#include <opencv2/opencv.hpp>
#include <hailo/hailort.hpp>
#include <hailo/hef.hpp>

#include <iostream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include "hailo_objects.hpp"
#include "hailo_common.hpp"
#include "pose_estimation/yolov8pose_postprocess.hpp"
#include "audio_player.h"
#include "gesture_ctl.h"
#include "gesture_timer.h"

#include <cstdlib>  // for aligned_alloc / free

constexpr size_t HAILO_ALIGNMENT = 16384;  // 16KB alignment required

// HailoRT's DMA engine requires memory to be aligned to 16384-byte boundaries
// when I tried to use regular std::vector the HW has to do extra copies -> degrading performance
// this is copied from someone else's project I found on github
struct AlignedBuffer {
    void* ptr = nullptr;
    size_t data_size = 0;    // actual data size the model expects
    size_t alloc_size = 0;   // rounded-up size for aligned_alloc

    AlignedBuffer(size_t sz) : data_size(sz) {
        alloc_size = ((sz + HAILO_ALIGNMENT - 1) / HAILO_ALIGNMENT) * HAILO_ALIGNMENT;
        ptr = aligned_alloc(HAILO_ALIGNMENT, alloc_size);
        if (!ptr) throw std::bad_alloc();
    }

    ~AlignedBuffer() { free(ptr); }

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    uint8_t* data() { return static_cast<uint8_t*>(ptr); }
};



int main(int argc, char* argv[]){

    // checking the arguments to see if I want to open a different neural network
    if (argc != 4){
	std::cerr << "Usage: " << argv[0] << " <path_to_hef> <camera_mode> <audio_file>\n";
	std::cerr << "camera mode: 0 = USB webcam, 1 = RPi AI camera\n";
	return 1;
    }

    std::string nn_path = argv[1];
    int camera_mode = std::stoi(argv[2]);
    std::string audio_path = argv[3];
    // std::cout << "The second argument was: " << nn_path << std::endl;

    // std::cout << "About to open the camera" << std::endl;
    cv::VideoCapture cap;

    if (camera_mode == 0){
	    std::cout << "Opening USB camera (/dev/video0)\n";
	    cap.open(0, cv::CAP_V4L2);
    } else if (camera_mode == 1) {
	    std::cout << "Opening RPi AI camera via libcamera\n";
	    std::string pipeline =
    		"libcamerasrc "
    		"! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 "
    		"! queue leaky=downstream max-size-buffers=1 "
    		"! videoconvert "
    		"! videoscale "
    		"! video/x-raw,width=640,height=640 "
    		"! videoconvert "
    		"! video/x-raw,format=BGR "
    		"! appsink drop=true max-buffers=1 sync=false";
	    cap.open(pipeline, cv::CAP_GSTREAMER);
    } else {
	    std::cerr << "Invalid camera_mode. Use 0 (USB) or 1 (RPi AI).\n";
            return 1;
    }

    // checking to make sure the capture was opened correctly
    if (!cap.isOpened()){
        std::cerr << "There was an error attempting to open the camera 😭" << std::endl;
        return 1;
    }
    std::cout << "The camera opened successfully" << std::endl;

    // std::cout << "about to create the hef" << std::endl;
    // checking to make sure the HEF (Hailo executable format) was created and 
    auto hef_exp = hailort::Hef::create(nn_path);                   
    if (!hef_exp){
        std::cerr << "There was an error creating hef_exp" << std::endl;
        return 1;
    }

    // std::cout << "hef_exp was created successfully" << std::endl;

    // std::cout << "About to extract the HEF object" << std::endl;
    hailort::Hef hef = std::move(hef_exp.value());
    std::cout << "The HEF object was successfully creted" << std::endl;

    // Create a VDevice (represents the physical Hailo chip)
    auto vdevice_exp = hailort::VDevice::create();
    if (!vdevice_exp){
        std::cerr << "There was an error creating the device" << std::endl;
        return 1;
    }
    auto vdevice = vdevice_exp.release();
    std::cout << "The device was created successfully" << std::endl;    

    // creating an inference model from the device
    auto infer_model = vdevice->create_infer_model(nn_path).expect("Failed to create infer model");
    auto configured_infer_model = infer_model->configure().expect("Failed to configure infer model");

    //input/output stream info -> used to query the HEF to find out the tensor shapes without hardcoding them
    auto input_streams_info = hef.get_input_vstream_infos();
    auto& input_vstream_info = input_streams_info.value()[0];
    if (!input_streams_info){
        std::cerr << "There was an error trying to get the info from the vstream" << std::endl;
        return 1;
    }

    // can get rid of, used for checking everything is alright
    for (auto& info : input_streams_info.value()){
        std::cout << "Info: " << info.name << " height:" << info.shape.height << " width:" << info.shape.width << " c:" << info.shape.features << std::endl;
    }

    // need to come back to figure what this is used for
    int net_h = input_streams_info.value()[0].shape.height;
    int net_w = input_streams_info.value()[0].shape.width;

    auto output_stream_info = hef.get_output_vstream_infos();
    //auto& output_vstream = output_stream_info.value()[0];
    if (!output_stream_info){
        std::cerr << "There was an error getting the output vstream info" << std::endl;
        return 1;
    }

    for (auto& info : output_stream_info.value()) {
        std::cout << "Output: " << info.name 
                << " height:" << info.shape.height 
                << " width:" << info.shape.width 
                << " features:" << info.shape.features
                << " format:" << (int)info.format.type
                << std::endl;
    }

    int output_size = output_stream_info.value()[0].shape.height * output_stream_info.value()[0].shape.width * output_stream_info.value()[0].shape.features;
    std::vector<uint8_t> output_data(output_size);
    // std::cout << "The output size for hef is " << output_size << std::endl;

    // var declarations
    cv::Mat frame;
    volatile int flag = 1;
    int lost_frames = 0;

    // // device info
    // std::cout << "Input stream name: '" << input_vstream_info.name << "'" << std::endl;
    // std::cout << "Output stream name: '" << output_vstream.name << "'" << std::endl;

    // pre-loop buffer allocation -> if done in the loop, would be more expensive, so I moved it out
    size_t input_size = net_h * net_w * 3;
    AlignedBuffer input_buf(input_size);
    std::map<std::string, AlignedBuffer> output_buffers;
    for (auto& info : output_stream_info.value()) {
        size_t sz = info.shape.height * info.shape.width * info.shape.features;
        output_buffers.emplace(info.name, sz);
    }

    // declarations for the prev points and checking to see if it is the first frame
    static std::vector<HailoPoint> prev_points;
    //static bool first_frame = true;

    // construct player and timer
    AudioPlayer player;
    player.loadFile(audio_path);
    player.setVolume(0.8f);
    player.play(-1);

    GestureTimer gTimer;

    float currentVolumeDb = -6.0f;
    float currentLP      = 800.0f;   // low-pass cutoff  (treble control)
    float currentHP      = 200.0f;   // high-pass cutoff (bass control)
    bool  reverbActive   = false;

    auto dbToLinear = [](float db) {
        return std::pow(10.0f, db / 20.0f);
    };

    // continuous loop for the camera
    while (flag){

        // checking to see if we lost any frames
        if (!cap.read(frame) || frame.empty()){
            lost_frames++;
            std::cout << "There was an error reading the frame, lost frame count is: " << lost_frames << std::endl;
            continue;
        }

        // creating a matrix to get the color
        cv::Mat resized, rgb;
        cv::resize(frame, resized, cv::Size(net_w, net_h));
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);


        // creating an input buffer for an inference call
        auto bindings = configured_infer_model.create_bindings().expect("Failed to create bindings");
        std::string input_name = input_vstream_info.name;
                
        // Inside the loop, copy frame data into it:
        std::memcpy(input_buf.data(), rgb.data, input_buf.data_size);
        bindings.input(input_name)->set_buffer(hailort::MemoryView(input_buf.data(), (size_t)input_buf.data_size));

        // Inside the loop (Bind all output buffers):
        for (auto& [name, buf] : output_buffers) {
            auto output_vstream = bindings.output(name);
            output_vstream->set_buffer(hailort::MemoryView(buf.data(), buf.data_size));
        }

        // submitting the job to the Hailo chip
        // std::cout << "Running inference..." << std::endl;
        auto job = configured_infer_model.run_async(bindings).expect("Failed to run inference");

        // blocks until the chip finishes (with a 1 sec timeout)
        // std::cout << "Waiting for job..." << std::endl;
        job.wait(std::chrono::milliseconds(1000));          // can change later if needed
	//auto status = job.wait(std::chrono::milliseconds(80));
	//if (status != hailort::HAILO_SUCCESS) {
    		// skip this frame, don't stall the pipeline
    	//	continue;
	//}

        // Build region of interest (ROI) covering the full frame
        auto roi = std::make_shared<HailoROI>(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f));

        // Add each output tensor to the ROI
        for (auto& info : output_stream_info.value()) {
            // auto& buf = output_buffers.at(info.name);
            auto& buf = output_buffers.at(std::string(info.name));

            // tells the postprocessor the shape, quantization parameters, and format of each tensor
            hailo_tensor_metadata_t meta;
            std::memcpy(meta.name, info.name, HAILO_MAX_STREAM_NAME_SIZE);
            meta.name[HAILO_MAX_STREAM_NAME_SIZE - 1] = '\0';
            
            // shape
            meta.shape.height = info.shape.height;
            meta.shape.width = info.shape.width;
            meta.shape.features = info.shape.features;
            
            // format
            meta.format.type = HailoTensorFormatType::HAILO_FORMAT_TYPE_UINT8;
            meta.format.is_nms = false;
            
            // quant info - get from the vstream info
            meta.quant_info.qp_zp = info.quant_info.qp_zp;
            meta.quant_info.qp_scale = info.quant_info.qp_scale;
            meta.quant_info.limvals_min = info.quant_info.qp_zp;   
            meta.quant_info.limvals_max = info.quant_info.qp_scale;

            // raw output bytes in each buffer need to be wrapped in HailoTensor objects so the postprocessor can read them
            auto tensor = std::make_shared<HailoTensor>(buf.data(), meta);
            roi->add_tensor(tensor);
        }

        // Run the postprocessor - decodes boxes, keypoints, NMS
        filter(roi);

        // Draw results on frame
        int frame_w = frame.cols;
        int frame_h = frame.rows;


        // loop for drawing and movement detection
        for (auto& obj : roi->get_objects_typed(HAILO_DETECTION)) {
            
            // bbox -> bounded box
            auto detection = std::dynamic_pointer_cast<HailoDetection>(obj);
            auto bbox = detection->get_bbox();

            // draw box
            int x1 = bbox.xmin() * frame_w;
            int y1 = bbox.ymin() * frame_h;
            int x2 = bbox.xmax() * frame_w;
            int y2 = bbox.ymax() * frame_h;
            cv::rectangle(frame, cv::Point(x1,y1), cv::Point(x2,y2),cv::Scalar(255,0,255), 2);

            // iterating over the landmarks 
            for (auto& subobj : detection->get_objects_typed(HAILO_LANDMARKS)) {

                auto landmarks = std::dynamic_pointer_cast<HailoLandmarks>(subobj);
                const auto& points = landmarks->get_points();

                // decided I only want the arm indices (5-10)
                // the other indices were for eyes, hips -> decided it was too much work to get these if we are not using them
                for (size_t k = 5; k <= 10; k++){
                    if (k < points.size() && points[k].confidence() > 0.5f){
                        int kx = points[k].x() * frame_w;
                        int ky = points[k].y() * frame_h;
                        cv::circle(frame, cv::Point(kx,ky), 6, cv::Scalar(0,255,0), -1);    // green dots
                    }
                }

                // draw skeleton
                std::vector<std::pair<int,int>> skeleton = {
                    {5, 7}, {7, 9},   // left:  shoulder->elbow->wrist
                    {6, 8}, {8, 10}   // right: shoulder->elbow->wrist
                };

                // iterating over the skeleton vector pairs - represnets keypoint indices to connect with a line
                for (auto& [a,b] : skeleton){

                    // bounds check
                    if (a < (int)points.size() && b < (int)points.size()) {

                        // if there is a high confidence (can change this later), draw the line
                        if (points[a].confidence() > 0.5f && points[b].confidence() > 0.5f){
                            cv::Point pa(points[a].x() * frame_w, points[a].y() * frame_h);
                            cv::Point pb(points[b].x() * frame_w, points[b].y() * frame_h);
                            cv::line(frame, pa, pb, cv::Scalar(255, 0, 255), 2);
                        }
                    }
                }

                // Movement detection for arm keypoints
                ArmKeypoints arms = extractArms(points, frame_w, frame_h);
                Gesture      g    = classifyGesture(arms, frame_w, frame_h);
                bool         fire = gTimer.update(g);

                if (fire) {
                    switch (g) {
                        case Gesture::ARMS_UP:
                            currentVolumeDb = std::min(0.0f, currentVolumeDb + 3.0f);  // +3 dB per hold
                            player.setVolume(dbToLinear(currentVolumeDb));
                            std::cout << "↑ Volume: " << currentVolumeDb << " dB\n";
                            break;

                        case Gesture::ARMS_DOWN:
                            currentVolumeDb = std::max(-40.0f, currentVolumeDb - 3.0f); // -3 dB per hold
                            player.setVolume(dbToLinear(currentVolumeDb));
                            std::cout << "↓ Volume: " << currentVolumeDb << " dB\n";
                            break;

                        case Gesture::TREBLE_UP:
                            // raise low-pass cutoff = let more treble through
                            currentLP = std::min(8000.0f, currentLP + 500.0f);
                            player.setLowPass(true, currentLP);
                            std::cout << "▲ Treble LP cutoff: " << currentLP << " Hz\n";
                            break;

                        case Gesture::BASS_UP:
                            // lower high-pass cutoff = let more bass through
                            currentHP = std::max(20.0f, currentHP - 50.0f);
                            player.setHighPass(true, currentHP);
                            std::cout << "▼ Bass HP cutoff: " << currentHP << " Hz\n";
                            break;

                        case Gesture::REVERB_ON:
                            reverbActive = !reverbActive;
                            player.setReverb(reverbActive);
                            std::cout << (reverbActive ? "🌊 Reverb ON" : "Reverb OFF") << "\n";
                            break;

                        default: break;
                    }
                }

            // draw gesture label on frame for debugging
            std::string label = "NONE";
            if (g == Gesture::ARMS_UP)    label = "ARMS UP";
            if (g == Gesture::ARMS_DOWN)  label = "ARMS DOWN";
            if (g == Gesture::TREBLE_UP)  label = "TREBLE";
            if (g == Gesture::BASS_UP)    label = "BASS";
            if (g == Gesture::REVERB_ON)  label = "REVERB";
            cv::putText(frame, label, cv::Point(20, 40),
            cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0,255,255), 2);
                

                prev_points = std::vector<HailoPoint>(points.begin(), points.end());
                //first_frame = false;
            }
        }

        // displaying the image
        cv::imshow("NN Feed", frame);

        // getting the keyboard press and checking if we need to exit
        int key = cv::waitKey(1);
        if (key == 27){
            std::cout << "User ended the program" << std::endl;
            flag = 0;
        }
    }

    // NOTE: can change the way it is sending to send on a timer instead of always sending

    // cleanup
    cap.release();
    cv::destroyAllWindows();
    return 0;
}
