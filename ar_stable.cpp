#include </home/nico/packages/eigen/Eigen/SVD>
#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/plot.hpp>
#include <iostream>
#include <fstream>
#include <ctime>

using namespace std;
using namespace cv;

namespace {
const char* about = "Basic marker detection";
const char* keys  =
        "{d        |       | dictionary: DICT_4X4_50=0, DICT_4X4_100=1, DICT_4X4_250=2,"
        "DICT_4X4_1000=3, DICT_5X5_50=4, DICT_5X5_100=5, DICT_5X5_250=6, DICT_5X5_1000=7, "
        "DICT_6X6_50=8, DICT_6X6_100=9, DICT_6X6_250=10, DICT_6X6_1000=11, DICT_7X7_50=12,"
        "DICT_7X7_100=13, DICT_7X7_250=14, DICT_7X7_1000=15, DICT_ARUCO_ORIGINAL = 16}"
        "{v        |       | Input from video file, if ommited, input comes from camera }"
        "{ci       | 0     | Camera id if input doesnt come from video (-v) }"
        "{c        |       | Camera intrinsic parameters. Needed for camera pose }"
        "{l        | 0.54  | Marker side lenght (in meters). Needed for correct scale in camera pose }"
        "{o        | 0.04  | Offset between markers (in meters) }"
        "{dp       |       | File of marker detector parameters }"
        "{r        |       | show rejected candidates too }"
        "{f        |       | Use stabilization filtering}"
        "{s        |       | Save results}";
}


static bool readCameraParameters(string filename, Mat &camMatrix, Mat &distCoeffs) {
    FileStorage fs(filename, FileStorage::READ);
    if(!fs.isOpened())
        return false;
    fs["camera_matrix"] >> camMatrix;
    fs["distortion_coefficients"] >> distCoeffs;
    return true;
}


static bool readDetectorParameters(string filename, Ptr<aruco::DetectorParameters> &params) {
    FileStorage fs(filename, FileStorage::READ);
    if(!fs.isOpened())
        return false;
    fs["adaptiveThreshWinSizeMin"] >> params->adaptiveThreshWinSizeMin;
    fs["adaptiveThreshWinSizeMax"] >> params->adaptiveThreshWinSizeMax;
    fs["adaptiveThreshWinSizeStep"] >> params->adaptiveThreshWinSizeStep;
    fs["adaptiveThreshConstant"] >> params->adaptiveThreshConstant;
    fs["minMarkerPerimeterRate"] >> params->minMarkerPerimeterRate;
    fs["maxMarkerPerimeterRate"] >> params->maxMarkerPerimeterRate;
    fs["polygonalApproxAccuracyRate"] >> params->polygonalApproxAccuracyRate;
    fs["minCornerDistanceRate"] >> params->minCornerDistanceRate;
    fs["minDistanceToBorder"] >> params->minDistanceToBorder;
    fs["minMarkerDistanceRate"] >> params->minMarkerDistanceRate;
    fs["cornerRefinementMethod"] >> params->cornerRefinementMethod;
    fs["cornerRefinementWinSize"] >> params->cornerRefinementWinSize;
    fs["cornerRefinementMaxIterations"] >> params->cornerRefinementMaxIterations;
    fs["cornerRefinementMinAccuracy"] >> params->cornerRefinementMinAccuracy;
    fs["markerBorderBits"] >> params->markerBorderBits;
    fs["perspectiveRemovePixelPerCell"] >> params->perspectiveRemovePixelPerCell;
    fs["perspectiveRemoveIgnoredMarginPerCell"] >> params->perspectiveRemoveIgnoredMarginPerCell;
    fs["maxErroneousBitsInBorderRate"] >> params->maxErroneousBitsInBorderRate;
    fs["minOtsuStdDev"] >> params->minOtsuStdDev;
    fs["errorCorrectionRate"] >> params->errorCorrectionRate;
    
    return true;
}


static Mat cvcloud_load()
{
    Mat cloud(1, 7708, CV_64FC3);
    ifstream ifs("arrow.ply");

    string str;
    for(size_t i = 0; i < 13; ++i)
        getline(ifs, str);

    Point3d* data = cloud.ptr<cv::Point3d>();
    float dummy1, dummy2, dummy3;
    for(size_t i = 0; i < 7708; ++i)
        ifs >> data[i].x >> data[i].y >> data[i].z >> dummy1 >> dummy2 >> dummy3;

    return cloud;
}


// Transform Rodrigues rotation vector into a quaternion
Vec4d vec2quat(Vec3d vec) {
    Vec4d q;
    double ang = sqrt( vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2] );
    q[0] = vec[0] / ang * sin(ang / 2);
    q[1] = vec[1] / ang * sin(ang / 2);
    q[2] = vec[2] / ang * sin(ang / 2);
    q[3] = cos(ang / 2);

    return q;
}


// Transform quaternion into a Rodrigues rotation vector
Vec3d quat2vec(Vec4d quat) {
    Vec3d v;
    double ang = 2*acos(quat[3]);
    v[0] = quat[0] / sqrt(1 - quat[3]*quat[3]) * ang;
    v[1] = quat[1] / sqrt(1 - quat[3]*quat[3]) * ang;
    v[2] = quat[2] / sqrt(1 - quat[3]*quat[3]) * ang;
    
    return v;
}

/// Compute average of two quaternions using 
// eq. (18) of https://ntrs.nasa.gov/archive/nasa/casi.ntrs.nasa.gov/20070017872.pdf, 
// that provides a closed-form solution for averaging two quaternions
Vec4d avgQuat(Vec4d q1, Vec4d q2, double w1 = 1, double w2 = 1) {
    Vec4d q3;
    double zed = sqrt( (w1-w2)*(w1-w2) + 4*w1*w2*(q1.dot(q2))*(q1.dot(q2)) );
    q3 = ((w1-w2+zed)*q1 + 2*w2*(q1.dot(q2))*q2);
    double norm = sqrt( q3[0]*q3[0] + q3[1]*q3[1] + q3[2]*q3[2] + q3[3]*q3[3] );
    q3 = q3 / norm;
    return q3;
}


// Method to find the average of a set of rotation quaternions using Singular Value Decomposition
/*
 * The algorithm used is described here:
 * https://ntrs.nasa.gov/archive/nasa/casi.ntrs.nasa.gov/20070017872.pdf
 */
Eigen::Vector4f quaternionAverage(std::vector<Eigen::Vector4f> quaternions)
{
    if (quaternions.size() == 0)
    {
        std::cerr << "Error trying to calculate the average quaternion of an empty set!\n";
        return Eigen::Vector4f::Zero();
    }

    // first build a 4x4 matrix which is the elementwise sum of the product of each quaternion with itself
    Eigen::Matrix4f A = Eigen::Matrix4f::Zero();

    for (unsigned long int q=0; q<quaternions.size(); ++q)
        A += quaternions[q] * quaternions[q].transpose();

    // normalise with the number of quaternions
    A /= quaternions.size();

    // Compute the SVD of this 4x4 matrix
    Eigen::JacobiSVD<Eigen::MatrixXf> svd(A, Eigen::ComputeThinU | Eigen::ComputeThinV);

    Eigen::VectorXf singularValues = svd.singularValues();
    Eigen::MatrixXf U = svd.matrixU();

    // find the eigen vector corresponding to the largest eigen value
    int largestEigenValueIndex;
    float largestEigenValue;
    bool first = true;

    for (int i=0; i<singularValues.rows(); ++i)
    {
        if (first)
        {
            largestEigenValue = singularValues(i);
            largestEigenValueIndex = i;
            first = false;
        }
        else if (singularValues(i) > largestEigenValue)
        {
            largestEigenValue = singularValues(i);
            largestEigenValueIndex = i;
        }
    }

    Eigen::Vector4f average;
    average(0) = U(0, largestEigenValueIndex);
    average(1) = U(1, largestEigenValueIndex);
    average(2) = U(2, largestEigenValueIndex);
    average(3) = U(3, largestEigenValueIndex);

    return average;
}


// Transform a relative translation into absolute
// (useful for augmented reality when we have offset wrt marker frame)
Vec3d transformVec(Vec3d vec, Vec3d rotvec, Vec3d tvec) {
    Mat rotationMat = Mat::zeros(3, 3, CV_64F);
    Mat transformMat = Mat::eye(4, 4, CV_64F);
    Rodrigues(rotvec, rotationMat); //convert rodrigues angles into rotation matrix
    
    //build transformation matrix
    for (int i=0; i<3; i++) {
        transformMat.at<double>(i,3) = tvec[i];
        for (int j=0; j<3; j++) {
            transformMat.at<double>(i,j) = rotationMat.at<double>(i,j);
        }
    }
    
    Vec4d vechomo; //vec in homogeneous coordinates, i.e. <x,y,z,1>
    vechomo[0] = vec[0];
    vechomo[1] = vec[1];
    vechomo[2] = vec[2];
    vechomo[3] = 1.0;

    Vec3d vectrans; //output, vector transformed
        Mat vectransMat = transformMat*Mat(vechomo);
    vectrans[0] = vectransMat.at<double>(0);
    vectrans[1] = vectransMat.at<double>(1);
    vectrans[2] = vectransMat.at<double>(2);

    return vectrans;
}

int main(int argc, char argv*[]) {
    CommandLineParser parser(argc, argv, keys);
    parser.about(about);


    if (argc < 2) {
        parser.printMessage();
        return 0;
    }


    // Parser
    int dictionaryId = parser.get<int>("d");
    bool showRejected = parser.has("r");
    bool estimatePose = parser.has("c");
    float markerLength = parser.get<float>("l");
    float markerOffset = parser.get<float>("o");
    bool stabilFilt = parser.has("f");
    bool saveResults = parser.has("s");


    // Detector parameters
    Ptr<aruco::DetectorParameters> detectorParams = aruco::DetectorParameters::create();
    if(parser.has("dp")) {
        bool readOk = readDetectorParameters(parser.get<string>("dp"), detectorParams);
        if(!readOk) {
            cerr << "Invalid detector parameters file" << endl;
            return 0;
        }
    }
    detectorParams->cornerRefinementMethod = aruco::CORNER_REFINE_SUBPIX;


    // Load video 
    String video;
    VideoCapture inputVideo;

    int waitTime;
    if(parser.has("v")) {
        video = parser.get<String>("v");
    }
    if(!parser.check()) {
        parser.printErrors();
        return 0;
    }
    if(!video.empty()) {
        inputVideo.open(video);
        waitTime = 1000 * 1.0 /inputVideo.get(CAP_PROP_FPS);
        // waitTime = 0; // wait for user for next frame
        cout << "Success: video loaded" << endl;
    } 
    else {
        inputVideo.open(0);
        waitTime = 1;
        cout << "Fail: video not found" << endl;
    }
    if(!inputVideo.isOpened()) {
        cout << "Video could not be opened..." << endl;
        return -1;
    }


    //Select dictionary for markers detection
    Ptr<aruco::Dictionary> dictionary =
        aruco::getPredefinedDictionary(aruco::PREDEFINED_DICTIONARY_NAME(dictionaryId));


    // Load camera parameters
    Mat camMatrix, distCoeffs;

    if(estimatePose) {
        bool readOk = readCameraParameters(parser.get<string>("c"), camMatrix, distCoeffs);
        if(!readOk) {
            cerr << "Invalid camera file" << endl;
            return 0;
        }
    }


    // Save results to video
    VideoWriter cap;
    int frame_width = inputVideo.get(CAP_PROP_FRAME_WIDTH);
    int frame_height = inputVideo.get(CAP_PROP_FRAME_HEIGHT);

    if (saveResults) cap.open("demo.avi", VideoWriter::fourcc('M', 'J', 'P', 'G'),
            inputVideo.get(CAP_PROP_FPS), Size(frame_width, frame_height));


    // Save results to file
    ofstream resultfile;

    if (stabilFilt && saveResults) {
        resultfile.open("results_filt.txt");
        if (resultfile.is_open()) {
            cout << "Filtered resulting transformations" << endl;
        }
        else cout << "Unable to open result file" << endl;
    }
    else if (!stabilFilt && saveResults) {
        resultfile.open("results_unfilt.txt");
        if (resultfile.is_open()) {
           cout << "Unfiltered resulting transformations" << endl;
        }
        else cout << "Unable to open result file" << endl;
    }


    // Load arrow point cloud
    Mat arrow_cloud = cvcloud_load();


    // Define variables
    double totalTime = 0;
    int totalIterations = 0;
    int frame_id = 0;

    vector<Point2d> arrow; // vec to print arrow on image plane

    // We have 12 markers
    vector<Vec3d> rvecs_ord(12); // store markers' Euler rotation vectors
    vector<Vec3d> tvecs_ord(12); // store markers' translation vectors

    // We have three big markers
    std::vector<bool> init_id(3, false); // check if marker has been seen before
    std::vector<int>  t_lost(3, 0); // count seconds from last time marker was seen
    std::vector<int>  t_stable(3, 0); // count seconds from moment markers are consistent

    // Weights for averaging final poses
    double alpha_rot = 0.7;
    double alpha_trasl = 0.7;


    //////* ---KEY PART--- *//////
    while(inputVideo.grab()) {

        Mat image, imageCopy;
        inputVideo.retrieve(image);
        //cv::resize(image, image, Size(image.cols/2, image.rows/2)); // lower video resolution

        cout << "Frame " << frame_id << endl;

        double tick = (double)getTickCount();

        vector<int> ids; // markers identified
        vector<vector<Point2f>> corners, rejected;
        vector<Vec3d> rvecs, tvecs; 

        // detect markers and estimate pose
        aruco::detectMarkers(image, dictionary, corners, ids, detectorParams, rejected);

        if(estimatePose && ids.size() > 0)
            aruco::estimatePoseSingleMarkers(corners, markerLength, camMatrix, distCoeffs, rvecs, tvecs);


        // Compute detection time
        double currentTime = ((double)getTickCount() - tick) / getTickFrequency();
        totalTime += currentTime;
        totalIterations++;
        if(totalIterations % 30 == 0) {
            cout << "Detection Time = " << currentTime * 1000 << " ms "
                 << "(Mean = " << 1000 * totalTime / double(totalIterations) << " ms)" << endl;
        }

        // draw results
        image.copyTo(imageCopy);

        // reorder rvecs and tvecs into rvecs_ord and tvecs_ord
        for(unsigned int i=0; i<rvecs.size(); i++) {
            rvecs_ord[ids[i]-1] = rvecs[i];
            tvecs_ord[ids[i]-1] = tvecs[i];
        }


        if(ids.size() > 0) {
            aruco::drawDetectedMarkers(imageCopy, corners, ids);

            aruco::drawAxis(imageCopy, camMatrix, distCoeffs, rvecs_ord[i], tvecs_ord[i], markerLength * 0.5f);
            }
        }
    }
}