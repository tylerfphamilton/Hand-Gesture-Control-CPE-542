#include <opencv2/opencv.hpp>
#include <hailo/hailort.hpp>
#include <hailo/hef.hpp>

#include <iostream>
#include <vector>
#include <cstring>
#include <stdexcept>

int main(int argc, char* argv[]){

    // checking the arguments to see if I want to open a different neural network
    if (argc != 2){
        std::cerr << "need two arguments, the object file and the path for the neural network" << std::endl;
    }

    std::string nn_path = argv[1];
    std::cout << "The second argument was: " << nn_path << std::endl;

    std::cout << "About to open the camera" << std::endl;
    cv::VideoCapture cap ("/dev/video0", cv::CAP_V4L2);
    
    if (!cap.isOpened()){
        std::cerr << "There was an error attempting to open the camera 😭" << std::endl;
        return 1;
    }

    std::cout << "The camera opened successfully" << std::endl;

    std::cout << "about to create the hef" << std::endl;
    auto hef_exp = hailort::Hef::create(nn_path);                   // 
    if (!hef_exp){
        std::cerr << "There was an error creating hef_exp" << std::endl;
        return 1;
    }

    std::cout << "hef_exp was created successfully" << std::endl;

    hailort::Hef hef = std::move(hef_exp.value());
    

    // var declarations
    cv::Mat frame;
    int flag = 1;
    int lost_frames = 0;

    // continuous loop for the camera
    while (flag){

        // checking to see if we lost any frames
        if (!cap.read(frame) || frame.empty()){
            lost_frames++;
            std::cout << "There was an error reading the frame, lost frame count is: " << lost_frames << std::endl;
            continue;
        }

        // std::cout << "Showing the image" << std::endl;
        cv::imshow("NN Feed", frame);

        // getting the keyboard press and checking if we need to exit
        int key = cv::waitKey(1);
        if (key == 27){
            std::cout << "User ended the program" << std::endl;
            flag = 0;
        }
    }

    // cleanup
    cap.release();
    cv::destroyAllWindows();
    return 0;
}

