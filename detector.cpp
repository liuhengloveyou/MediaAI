#include <iostream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <random>

#include "inference.h"
#include "detector.h"
#include "frame_utils.h"
#include "yolov8_utils.h"
#include "yolov8_opencv_detector.h"

#define IMG_SIZE 640
using namespace std;

static YOLO_V8 *yoloDetector;
static Yolov8CV *cvdetect;

// void Detector(YOLO_V8 *&p)
// {
//     std::filesystem::path current_path = std::filesystem::current_path();
//     std::filesystem::path imgs_path = current_path / "images";
//     for (auto &i : std::filesystem::directory_iterator(imgs_path))
//     {
//         if (i.path().extension() == ".jpg" || i.path().extension() == ".png" || i.path().extension() == ".jpeg")
//         {
//             std::string img_path = i.path().string();
//             cv::Mat img = cv::imread(img_path);

//             // DetectorMat(img);

//             std::cout << "Press any key to exit" << std::endl;
//             cv::imshow("Result of Detection", img);
//             cv::waitKey(0);
//             cv::destroyAllWindows();
//         }
//     }
// }

template <typename _Tp>
int yolov8_cv_detect(_Tp &cls, cv::Mat &img)
{
    // 生成随机颜色
    std::vector<cv::Scalar> color;
    srand(time(0));
    for (int i = 0; i < 80; i++)
    {
        int b = rand() % 256;
        int g = rand() % 256;
        int r = rand() % 256;
        color.push_back(cv::Scalar(b, g, r));
    }

    vector<OutputSeg> result;
    if (cls.detect(img, result))
    {
        DrawPred(img, result, cls._className, color);
    }
    else
    {
        cout << "Detect Failed!" << std::endl;
    }

    return 0;
}

template <typename _Tp>
int yolov8_onnx(_Tp &cls, cv::Mat &img, string &model_path)
{
    if (cls.ReadModel(model_path, false))
    {
        cout << "read net ok!" << std::endl;
    }
    else
    {
        return -1;
    }
    // 生成随机颜色
    std::vector<cv::Scalar> color;
    srand(time(0));
    for (int i = 0; i < 80; i++)
    {
        int b = rand() % 256;
        int g = rand() % 256;
        int r = rand() % 256;
        color.push_back(cv::Scalar(b, g, r));
    }
    std::vector<OutputSeg> result;
    if (cls.OnnxDetect(img, result))
    {
        DrawPred(img, result, cls._className, color);
    }
    else
    {
        std::cout << "Detect Failed!" << std::endl;
    }

    return 0;
}

AVFrame *detect(AVFrame *frame)
{
    if (!frame)
    {
        return nullptr;
    }

    cv::Mat img = AVFrameToCvMat(frame);

    yolov8_cv_detect(*cvdetect, img);

    // std::vector<DL_RESULT> res;
    // yoloDetector->RunSession(img, res);

    // for (auto &re : res)
    // {
    //     cv::RNG rng(cv::getTickCount());
    //     cv::Scalar color(rng.uniform(0, 256), rng.uniform(0, 256), rng.uniform(0, 256));

    //     cv::rectangle(img, re.box, color, 3);

    //     float confidence = floor(100 * re.confidence) / 100;
    //     std::cout << std::fixed << std::setprecision(2);
    //     std::string label = yoloDetector->classes[re.classId] + " " + std::to_string(confidence).substr(0, std::to_string(confidence).size() - 4);

    //     cv::rectangle(
    //         img,
    //         cv::Point(re.box.x, re.box.y - 25),
    //         cv::Point(re.box.x + label.length() * 15, re.box.y),
    //         color,
    //         cv::FILLED);

    //     cv::putText(
    //         img,
    //         label,
    //         cv::Point(re.box.x, re.box.y - 5),
    //         cv::FONT_HERSHEY_SIMPLEX,
    //         0.75,
    //         cv::Scalar(0, 0, 0),
    //         2);
    // }

    return CVMatToAVFrame(img);
}

int ReadCocoYaml(YOLO_V8 *&p)
{
    // Open the YAML file
    std::ifstream file("coco.yaml");
    if (!file.is_open())
    {
        std::cerr << "Failed to open file" << std::endl;
        return 1;
    }

    // Read the file line by line
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(file, line))
    {
        lines.push_back(line);
    }

    // Find the start and end of the names section
    std::size_t start = 0;
    std::size_t end = 0;
    for (std::size_t i = 0; i < lines.size(); i++)
    {
        if (lines[i].find("names:") != std::string::npos)
        {
            start = i + 1;
        }
        else if (start > 0 && lines[i].find(':') == std::string::npos)
        {
            end = i;
            break;
        }
    }

    // Extract the names
    std::vector<std::string> names;
    for (std::size_t i = start; i < end; i++)
    {
        std::stringstream ss(lines[i]);
        std::string name;
        std::getline(ss, name, ':'); // Extract the number before the delimiter
        std::getline(ss, name);      // Extract the string after the delimiter
        names.push_back(name);
    }

    p->classes = names;
    return 0;
}

int InitDetect(std::string modelPath)
{
    std::string model_path = "./models/yolov8n.onnx";
	cvdetect = new Yolov8CV(model_path);
    return 0;

    yoloDetector = new YOLO_V8;
    ReadCocoYaml(yoloDetector);

    DL_INIT_PARAM params;
    params.rectConfidenceThreshold = 0.1;
    params.iouThreshold = 0.5;
    params.modelPath = modelPath;
    params.imgSize = {IMG_SIZE, IMG_SIZE};
#ifdef USE_CUDA
    params.cudaEnable = true;

    // GPU FP32 inference
    params.modelType = YOLO_DETECT_V8;
    // GPU FP16 inference
    // Note: change fp16 onnx model
    // params.modelType = YOLO_DETECT_V8_HALF;

#else
    // CPU inference
    params.modelType = YOLO_DETECT_V8;
    params.cudaEnable = false;

#endif
    yoloDetector->CreateSession(params);

    return 0;
}