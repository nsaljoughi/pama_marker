#include </home/nicola/Packages/eigen/Eigen/SVD>
#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/plot.hpp>
#include <iostream>
#include <fstream>
#include <ctime>
#include <cmath>
#include <math.h>
#include <limits>

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
        "{l        |       | Marker side lenght (in meters). Needed for correct scale in camera pose }"
        "{o        |       | Offset between markers (in meters) }"
        "{dp       |       | File of marker detector parameters }"
        "{r        |       | show rejected candidates too }"
        "{n        | false | Naive mode (no stabilization)}"
        "{s        |       | Save results}"
        "{u        |       | Use-case / scenario (0, 1, 2, 3, 4, 5)}";
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


Mat create_bbox(double x_scale, double y_scale, double z_scale) 
{
    Mat cloud(1, 4, CV_64FC3);
    Point3d* bbox = cloud.ptr<cv::Point3d>();

    bbox[0].x = - 1.0 * (x_scale / 2.0);
    bbox[0].y = - 1.0 * (y_scale / 2.0);
    bbox[0].z = z_scale / 2.0;
    bbox[1].x = x_scale / 2.0;
    bbox[1].y = - 1.0 * (y_scale / 2.0);
    bbox[1].z = z_scale / 2.0;
    bbox[2].x = x_scale / 2.0;
    bbox[2].y = y_scale / 2.0;
    bbox[2].z = z_scale / 2.0;
    bbox[3].x = - 1.0 * (x_scale / 2.0);
    bbox[3].y = y_scale / 2.0;
    bbox[3].z = z_scale / 2.0;

    return cloud;
}


// Get angle from Rodrigues vector
double getAngle(Vec3d rvec) {
    double theta = sqrt( rvec[0]*rvec[0] + rvec[1]*rvec[1] + rvec[2]*rvec[2] );

    return theta;
}

Vec3d rodrigues2euler(Vec3d rvec, bool degrees=false) {
    Vec3d rvec_euler;
    double angle;
    double x, y, z;

    angle = getAngle(rvec);
    x = rvec[0] / angle;
    y = rvec[1] / angle;
    z = rvec[2] / angle; 

    double s=sin(angle);
    double c=cos(angle);
    double t=1-c;
    double heading, attitude, bank;

    if ((x*y*t + z*s) > 0.998) { // north pole singularity detected
        heading = 2*atan2(x*sin(angle/2), cos(angle/2));
        attitude = M_PI/2;
        bank = 0;
        rvec_euler[0] = heading;
        rvec_euler[1] = attitude;
        rvec_euler[2] = bank;

        if(degrees) rvec_euler*=(180.0/M_PI);

        return rvec_euler;
    }
    if ((x*y*t + z*s) < -0.998) { // south pole singularity detected
        heading = -2*atan2(x*sin(angle/2), cos(angle/2));
        attitude = -M_PI/2;
        bank = 0;
        rvec_euler[0] = heading;
        rvec_euler[1] = attitude;
        rvec_euler[2] = bank;

        if(degrees) rvec_euler*=(180.0/M_PI);

        return rvec_euler;
    }
    heading = atan2(y * s- x * z * t , 1 - (y*y+ z*z ) * t);
    attitude = asin(x * y * t + z * s) ;
    bank = atan2(x * s - y * z * t , 1 - (x*x + z*z) * t);

    rvec_euler[0] = heading;
    rvec_euler[1] = attitude;
    rvec_euler[2] = bank;

    if(degrees) rvec_euler*=(180.0/M_PI);

    return rvec_euler;
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
Eigen::Vector4f vec2quat_eigen(Vec3d vec) {
    Eigen::Vector4f q;
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
Vec3d quat_eigen2vec(Eigen::Vector4f quat) {
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
// Transform a relative point into absolute
Point3d transformPoint(Point3d vec, Vec3d rotvec, Vec3d tvec) {
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
    vechomo[0] = vec.x;
    vechomo[1] = vec.y;
    vechomo[2] = vec.z;
    vechomo[3] = 1.0;

    Point3d vectrans; //output, vector transformed
    Mat vectransMat = transformMat*Mat(vechomo);
    vectrans.x = vectransMat.at<double>(0);
    vectrans.y = vectransMat.at<double>(1);
    vectrans.z = vectransMat.at<double>(2);

    return vectrans;
}



// Avg two poses with weights associated

Vec3d avgRot(Vec3d rvec1, Vec3d rvec2, double weight1, double weight2) {
    Vec4d quat1 = vec2quat(rvec1);
    Vec4d quat2 = vec2quat(rvec2);
    Vec4d quat_avg = avgQuat(quat1, quat2, weight1, weight2);

    return quat2vec(quat_avg);
}

Vec3d avgTrasl(Vec3d tvec1, Vec3d tvec2, double weight1, double weight2) {
    Vec3d tvec_avg;
    for (int i=0; i<3; i++) {
        tvec_avg[i] = weight1*tvec1[i] + weight2*tvec2[i];
    }

    return tvec_avg; 
}


// Check diff between two rotations in Euler notation
bool checkDiffRot(Vec3d rvec1, Vec3d rvec2, std::vector<double> thr) {
    Vec3d rvec1_eul = rodrigues2euler(rvec1);
    Vec3d rvec2_eul = rodrigues2euler(rvec2);

    for(int i=0; i<3; i++) {
        if(std::abs(sin(rvec1_eul[i])-sin(rvec2_eul[i])) > thr[i]) {
            return false;
        }
    }
    return true;
}


// Compute combination pose at center of marker group
Vec3d computeAvgRot(std::vector<Vec3d> rvecs_ord, std::vector<bool> detect_id, int group) {
    std::vector<Eigen::Vector4f> quat_avg;
    Vec3d rvec_avg;
    for(unsigned int i=0; i<4; i++) {
        Eigen::Vector4f quat;
        if(detect_id[group*4+i]) {
            quat = vec2quat_eigen(rvecs_ord[group*4+i]);
            quat_avg.push_back(quat);
        }
    }
    rvec_avg = quat_eigen2vec(quaternionAverage(quat_avg));

    return rvec_avg;
}

Vec3d computeAvgTrasl(std::vector<Vec3d> tvecs_ord, std::vector<Vec3d> rvecs_ord, 
                      std::vector<bool> detect_id, int group, float markerLength, float markerOffset) {
    std::vector<Vec3d> tvecs_centered;
    Vec3d tvec_avg;

    if(group==0 || group==1 || group==3) { // markers in a square
        for(unsigned int i=0; i<4; i++) {
            Vec3d tvec;
            if(detect_id[group*4+i]) {
                if(i==0) {
                    tvec[0] = markerLength / 2 + markerOffset / 2;
                    tvec[1] = -1.0 * (markerLength / 2 + markerOffset / 2);
                    tvec[2] = 0.0;
                    tvec = transformVec(tvec, rvecs_ord[group*4+i], tvecs_ord[group*4+i]);
                    tvecs_centered.push_back(tvec);
                }
                else if(i==1) {
                    tvec[0] = markerLength / 2 + markerOffset / 2;
                    tvec[1] = markerLength / 2 + markerOffset / 2;
                    tvec[2] = 0.0;
                    tvec = transformVec(tvec, rvecs_ord[group*4+i], tvecs_ord[group*4+i]);
                    tvecs_centered.push_back(tvec);
                }
                else if(i==2) {
                    tvec[0] = -1.0 * (markerLength / 2 + markerOffset / 2);
                    tvec[1] = -1.0 * (markerLength / 2 + markerOffset / 2);
                    tvec[2] = 0.0;
                    tvec = transformVec(tvec, rvecs_ord[group*4+i], tvecs_ord[group*4+i]);
                    tvecs_centered.push_back(tvec);
                }
                else if(i==3) {
                    tvec[0] = -1.0 * (markerLength / 2 + markerOffset / 2);
                    tvec[1] = markerLength / 2 + markerOffset / 2;
                    tvec[2] = 0.0;
                    tvec = transformVec(tvec, rvecs_ord[group*4+i], tvecs_ord[group*4+i]);
                    tvecs_centered.push_back(tvec);
                }
            }
        }
    }
    else { // markers in line
        for(unsigned int i=0; i<4; i++) {
            Vec3d tvec;
            if(detect_id[group*4+i]) {
                if(i==0) {
                    tvec[0] = 1.5*markerLength + 1.5*markerOffset;
                    tvec[1] = tvec[2] = 0.0;
                    tvec = transformVec(tvec, rvecs_ord[group*4+i], tvecs_ord[group*4+i]);
                    tvecs_centered.push_back(tvec);
                }
                else if(i==1) {
                    tvec[0] = markerLength / 2 + markerOffset / 2;
                    tvec[1] = tvec[2] = 0.0;
                    tvec = transformVec(tvec, rvecs_ord[group*4+i], tvecs_ord[group*4+i]);
                    tvecs_centered.push_back(tvec);
                }
                else if(i==2) {
                    tvec[0] = -1.0 * (markerLength / 2 + markerOffset / 2);
                    tvec[1] = tvec[2] = 0.0;
                    tvec = transformVec(tvec, rvecs_ord[group*4+i], tvecs_ord[group*4+i]);
                    tvecs_centered.push_back(tvec);
                }
                else if(i==3) {
                    tvec[0] = -1.0 * (1.5*markerLength + 1.5*markerOffset);
                    tvec[1] = tvec[2] = 0.0;
                    tvec = transformVec(tvec, rvecs_ord[group*4+i], tvecs_ord[group*4+i]);
                    tvecs_centered.push_back(tvec);
                }
            }
        }
    }

    for (int i=0; i<3; i++) {
        tvec_avg[i] = 0.0;
        for (unsigned int j=0; j<tvecs_centered.size(); j++) {
            tvec_avg[i] += tvecs_centered[j][i];
        }
        tvec_avg[i] /= tvecs_centered.size();
    }

    return tvec_avg;
} 



// Check if num markers' poses are consistent
std::vector<bool> checkPoseConsistent(std::vector<Vec3d> rvecs_ord, std::vector<bool> detect_id, unsigned int num, 
                         int group, std::vector<double> thr) {
    cout << "Checking markers' consistency for GROUP " << group << endl;
    std::vector<bool> checkVec = detect_id;
    std::vector<Vec3d> rvecs;
    unsigned int items=0;

    
    for(int i=0; i<4; i++) {
        rvecs.push_back(rodrigues2euler(rvecs_ord[group*4+i]));
        Mat rotationMat = Mat::zeros(3, 3, CV_64F);
        Rodrigues(rvecs_ord[group*4+i], rotationMat); //convert rodrigues angles into rotation matrix
        cout << "[" ;
        for(int i = 0; i<3; i++) {
            for(int j=0; j<3; j++) {
                cout << rotationMat.at<double>(i,j) << " ";
            }
            cout << endl;
        }
        cout << "]" << endl;

        if(detect_id[group*4+i]) {
            items += 1;
        }
    }

    if(items < num) {
        for(int i=0; i<4; i++) {
            checkVec[group*4+i] = false;
        }
        return checkVec;
    }
    
    cout << "Detected markers to compare: " << items << endl;
    std::vector<std::vector<bool>> checker(rvecs.size(), std::vector<bool>(rvecs.size(), true));

    for(unsigned int i=0; i<rvecs.size(); i++) {
        if(!detect_id[group*4+i]) {
            checker[0][i] = checker[1][i] = checker[2][i] = checker[3][i] = false;
            continue;
        }
        for(unsigned int j=0; j<rvecs.size(); j++) {
            if(i==j) continue;
            if(!detect_id[group*4+j]) {
                checker[i][j] = false;
                continue;
            }

            for(int k=0; k<3; k++) {
                cout << "Diff between angles: "
                << std::abs(sin(rvecs[i][k])-sin(rvecs[j][k]))
                << " > " << thr[k] << "?" << endl;
                if(std::abs(sin(rvecs[i][k])-sin(rvecs[j][k])) > thr[k]) {
                    cout << "YES!!" << endl;
                    checker[i][j] = false;
                    break;
                }
                else {
                    cout << "No, OK. " << endl;
                    checker[i][j] = true;
                }
            }
        }
    }


    for(unsigned int i=0; i<rvecs.size(); i++) {
        unsigned int trues=0;
        unsigned int falses=0;

        // count how many markers are consistent with current one
        for(unsigned int j=0; j<rvecs.size(); j++) {
            if(i==j) continue;
            if(!checker[i][j]) {
                falses += 1;
            }
            else {
                trues += 1;
            }
        }

        // If it agrees with all markers, keep it
        if(trues >= (num-1)) { 
            checkVec[group*4+i] = true;
            continue;
        }
        else {
            checkVec[group*4+i] = false;
            continue;
        }
    }

    cout << "Checker: ";
    for(unsigned int i=0; i<rvecs.size();i++){
        
        for(auto&& j:checker[i]) {
            cout << j << " ";
        }
    }
    return checkVec;
}


// Given a vector of 2d points, draw a semi-transparent box
void DrawBox2D(Mat imageCopy, vector<Point2d> box1, int b_ch, int r_ch, int g_ch) {

    line(imageCopy, box1[0], box1[1], Scalar(b_ch,r_ch,g_ch), 2, LINE_8);
    line(imageCopy, box1[1], box1[2], Scalar(b_ch,r_ch,g_ch), 2, LINE_8);
    line(imageCopy, box1[2], box1[3], Scalar(b_ch,r_ch,g_ch), 2, LINE_8);
    line(imageCopy, box1[3], box1[0], Scalar(b_ch,r_ch,g_ch), 2, LINE_8);

    Point face1[1][4];

    face1[0][0] = Point(box1[0].x, box1[0].y);
    face1[0][1] = Point(box1[1].x, box1[1].y);
    face1[0][2] = Point(box1[2].x, box1[2].y);
    face1[0][3] = Point(box1[3].x, box1[3].y);

    const Point* boxppt1[1] = {face1[0]};

    int npt[] = {4};
    double alpha = 0.3;

    Mat overlay1;
    imageCopy.copyTo(overlay1);
    fillPoly(overlay1, boxppt1, npt, 1, Scalar(b_ch,r_ch,g_ch), LINE_8);
    addWeighted(overlay1, alpha, imageCopy, 1-alpha, 0, imageCopy);
}

// Function to average boxes
vector<Point2d> avgBoxes(vector<vector<Point2d>> boxes, vector<double> weights) {
    vector<Point2d> avg_box(8);
    avg_box[0].x = 0.0;
    avg_box[0].y = 0.0;
    avg_box[1].x = 0.0;
    avg_box[1].y = 0.0;
    avg_box[2].x = 0.0;
    avg_box[2].y = 0.0;
    avg_box[3].x = 0.0;
    avg_box[3].y = 0.0;

    for(unsigned int i=0; i<boxes.size(); i++) {
	    avg_box[0].x += boxes[i][0].x * weights[i];
            avg_box[0].y += boxes[i][0].y * weights[i];
            avg_box[1].x += boxes[i][1].x * weights[i];
            avg_box[1].y += boxes[i][1].y * weights[i];
            avg_box[2].x += boxes[i][2].x * weights[i];
            avg_box[2].y += boxes[i][2].y * weights[i];
            avg_box[3].x += boxes[i][3].x * weights[i];
            avg_box[3].y += boxes[i][3].y * weights[i];
    }
    for(int i=0; i<8; i++) {
        cout << avg_box[i].x << ", " << avg_box[i].y << endl;
    }

    return avg_box;
}




//////
int main(int argc, char *argv[]) {
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
    bool naiveMode = parser.get<bool>("n");
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
    detectorParams->cornerRefinementMethod = aruco::CORNER_REFINE_CONTOUR;


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
    
    // Get frame width and height
    int frame_width = inputVideo.get(CAP_PROP_FRAME_WIDTH);
    int frame_height = inputVideo.get(CAP_PROP_FRAME_HEIGHT);
    cout << "Frame size: " << frame_width << "x" << frame_height << endl;
    
    // Save results to video
    VideoWriter cap;
    if (saveResults) {
        cap.open("demo.avi", VideoWriter::fourcc('M', 'J', 'P', 'G'),
                inputVideo.get(CAP_PROP_FPS), Size(frame_width, frame_height));
    }


    // Save results to file
    ofstream resultfile;

    if (!naiveMode && saveResults) {
        resultfile.open("results_filt.txt");
        if (resultfile.is_open()) {
            cout << "Filtered resulting transformations" << endl;
        }
        else {
            cout << "Unable to open result file" << endl;
        }
    }
    else if (naiveMode && saveResults) {
        resultfile.open("results_unfilt.txt");
        if (resultfile.is_open()) {
           cout << "Unfiltered resulting transformations" << endl;
        }
        else {
            cout << "Unable to open result file" << endl;
        }
    }


    // Load arrow point cloud
    Mat arrow_cloud = cvcloud_load();

    Mat box_cloud = create_bbox(3.0, 2.0, 1.0);
    Mat tvec3d(1, 1, CV_64FC3);
    Point3d* pt3d = tvec3d.ptr<cv::Point3d>();
    pt3d[0].x = 0.0;
    pt3d[0].y = 0.0;
    pt3d[0].z = 0.0;
    vector<Point2d> tvec2d;



    // Define variables
    double totalTime = 0;
    int totalIterations = 0;

    double abs_tick = (double)getTickCount();
    double delta_t = 0;

    vector<Point2d> box, box1, box2, box3, box4, box5, box6, box7, box8, box9, box10, box11, box12; // vec to print arrow on image plane
    

    // We have four big markers
    std::vector<double>  t_lost(4, 0); // count seconds from last time marker was seen
    std::vector<double>  t_stable(4, 0); // count seconds from moment markers are consistent
    double thr_lost = 2; // TODO threshold in seconds for going into init
    double thr_stable = 0.5; // TODO threshold in seconds for acquiring master pose
    int consist_markers = 4;

    // Weights for averaging final poses
    double alpha_rot = 0.1;
    double alpha_trasl = 0.1;
    std::vector<double> thr_init(3); // TODO angle threshold for markers consistency in INIT
    std::vector<double> thr_noinit(3); // TODO angle threshold for markers consistency AFTER INIT
    thr_init[0] = (sin(M_PI/12.0));
    thr_init[1] = (sin(M_PI/12.0));
    thr_init[2] = (sin(M_PI/12.0));
    thr_noinit[0] = (sin(M_PI/12.0));
    thr_noinit[1] = (sin(M_PI/12.0));
    thr_noinit[2] = (sin(M_PI/12.0));

    
    if(naiveMode) {
        thr_init[0] = thr_init[1] = thr_init[2] = thr_noinit[0] = thr_noinit[1] = thr_noinit[2] = 2.0;
        thr_lost = std::numeric_limits<double>::max();
        thr_stable = 0.0;
        consist_markers = 1.0;
    }

    // One master pose for each group
    vector<Vec3d> rMaster(4);
    vector<Vec3d> tMaster(4);
    // One pose for the whole scene
    Vec3d rScene;
    Vec3d tScene;
    std::vector<bool> init_id(16, false); // check if marker has been seen before
    Vec3d a_avg, b_avg, c_avg, d_avg;
    
    bool average = false; //flag to decide whether to average or not


    ////// ---KEY PART--- //////
    while(inputVideo.grab()) {

        double tickk = (double)getTickCount();

        Mat image, imageCopy;
        inputVideo.retrieve(image);
        //cv::resize(image, image, Size(image.cols/2, image.rows/2)); // lower video resolution
    
        // We have 16 markers
        vector<Vec3d> rvecs_ord(16); // store markers' Euler rotation vectors
        vector<Vec3d> tvecs_ord(16); // store markers' translation vectors
        std::vector<bool> detect_id(16, true); // check if marker was detected or not

        cout << "Frame " << totalIterations << endl;
        cout << "abs_tick" << ((double)getTickCount() - abs_tick) / getTickFrequency() << endl;

        double tick = (double)getTickCount();
        double delta = 0;

	
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

            // Loop over markers
            for(unsigned int i=0; i<16; i++) {
                cout << "Group " << ceil(i/4) << endl;

                // check if marker was detected
                if(rvecs_ord[i][0] == 0.0) { 
                    detect_id[i] = false;
                    continue;
                }

                // if not initialized, go on with other markers
                if(!init_id[i]) {
                    continue;
                }

                else if(!checkDiffRot(rvecs_ord[i], rMaster[ceil(i/4)], thr_init)) {
                    Mat rotationMat = Mat::zeros(3, 3, CV_64F);
                    Rodrigues(rvecs_ord[i], rotationMat); //convert rodrigues angles into rotation matrix
                    cout << "Marker is wrong! " << endl;
                    cout <<  "Rvec matrix: " << endl;
                    cout << "[" ;
                    for(int i = 0; i<3; i++) {
                        for(int j=0; j<3; j++) {
                            cout << rotationMat.at<double>(i,j) << " ";
                        }
                        cout << endl;
                    }
                    cout << "]" << endl;
                    cout << "rMaster matrix: " << endl; 
                    Mat rotationMatM = Mat::zeros(3, 3, CV_64F);
                    Rodrigues(rMaster[ceil(i/4)], rotationMatM); //convert rodrigues angles into rotation matrix
                    cout << "[" ;
                    for(int i = 0; i<3; i++) {
                        for(int j=0; j<3; j++) {
                            cout << rotationMatM.at<double>(i,j) << " ";
                        }
                        cout << endl;
                    }
                    cout << "]" << endl;

                    detect_id[i] = false;
                    continue;
                }
                
                aruco::drawAxis(imageCopy, camMatrix, distCoeffs, rvecs_ord[i], tvecs_ord[i], markerLength * 0.5f);
            }


            // Loop over groups
            for(unsigned int i=0; i<4; i++) {
                if(!init_id[i*4]) { // if group needs init
                    cout << "GROUP " << i << ": initializing..." << endl;

                    std::vector<bool> detect_id_check = checkPoseConsistent(rvecs_ord, detect_id, 4, i, thr_init);

                    for(int j=0; j<16; j++) {
                        detect_id[j] = detect_id_check[j];
                    }

                    int counter=0;

                    for(int j=0; j<4; j++) {
                        if(detect_id[i*4+j]) {
                            counter += 1;
                        } 
                    }

                    cout << "In group " << i << " there are " << counter << " consistent markers" << endl;

                    if(counter >= consist_markers) { // if n markers are consistent
                        t_stable[i] += delta_t;
                        if(t_stable[i] >= thr_stable) {
                            init_id[i*4] = init_id[i*4+1] = init_id[i*4+2] = init_id[i*4+3] = true;
                            rMaster[i] = computeAvgRot( rvecs_ord, detect_id, i);
                            tMaster[i] = computeAvgTrasl(tvecs_ord, rvecs_ord, detect_id, i, markerLength, markerOffset);
                            t_stable[i] = 0;
                        }
                        else {
                            init_id[i*4] = init_id[i*4+1] = init_id[i*4+2] = init_id[i*4+3] = false;
                        }
                    }
                    else {
                        t_stable[i] = 0;
                    }
                } // if already init
                else {
                    cout << "GROUP " << i << " is already initialized." << endl;
                    if(!detect_id[i*4] && !detect_id[i*4+1] && !detect_id[i*4+2] && !detect_id[i*4+3]) {
                        t_lost[i] += delta_t;
                        if(t_lost[i] >= thr_lost) {
                            init_id[i*4] = init_id[i*4+1] = init_id[i*4+2] = init_id[i*4+3] = false;
                            t_lost[i] = 0;
			    average=false;    
    			}
                    }
                    else{
                        rMaster[i] = avgRot(computeAvgRot(rvecs_ord, detect_id, i), rMaster[i], alpha_rot, (1 - alpha_rot));
                        tMaster[i] = avgTrasl(computeAvgTrasl(tvecs_ord, rvecs_ord, detect_id, i, markerLength, markerOffset), tMaster[i], alpha_trasl, (1 - alpha_trasl));
                        //rMaster[i] = computeAvgRot( rvecs_ord, detect_id, i);
                        //tMaster[i] = computeAvgTrasl(tvecs_ord, rvecs_ord, detect_id, i, markerLength, markerOffset);
                    }
                }
            }
            
            Vec3d a0, b0, c0, d0, a1, b1, c1, d1, a3, b3, c3, d3;
           
            a0[0] = -1.6;
            a0[1] = -10.7 + 0.5;
            a0[2] = -3;
            b0[0] = -1.6;
            b0[1] = -10.7 + 0.5;
            b0[2] = -43;
            c0[0] = -1.6;
            c0[1] = 9.3 + 0.5;
            c0[2] = -23;
            d0[0] = -1.6;
            d0[1] = -30.7 + 0.5;
            d0[2] = -23;
        
            a1[0] = 0.0 - 0.5;
            a1[1] = -1.5;
            a1[2] = -3;
            b1[0] = 0.0 - 0.5;
            b1[1] = -1.5;
            b1[2] = -43;
            c1[0] = -20 - 0.5;
            c1[1] = -1.5;
            c1[2] = -23;
            d1[0] = 20 - 0.5;
            d1[1] = -1.5;
            d1[2] = -23;
        
            a3[0] = 10.8+0.5;
            a3[1] = 1;
            a3[2] = -3;
            b3[0] = 10.8+0.5;
            b3[1] = 1;
            b3[2] = -43;
            c3[0] = 30.8+0.5;
            c3[1] = 1;
            c3[2] = -23;
            d3[0] = -9.2+0.5;
            d3[1] = 1;
            d3[2] = -23;
       
            a0 = transformVec(a0, rMaster[0], tMaster[0]);
            b0 = transformVec(b0, rMaster[0], tMaster[0]);
            c0 = transformVec(c0, rMaster[0], tMaster[0]);
            d0 = transformVec(d0, rMaster[0], tMaster[0]);
        
            a1 = transformVec(a1, rMaster[1], tMaster[1]);
            b1 = transformVec(b1, rMaster[1], tMaster[1]);
            c1 = transformVec(c1, rMaster[1], tMaster[1]);
            d1 = transformVec(d1, rMaster[1], tMaster[1]);
       

            resultfile << "Frame " << totalIterations << ", " <<
            "x " << tMaster[0][0] << ", " <<
            "y " << tMaster[0][1] << ", " <<
            "z " << tMaster[0][2] << "; " << "\n";
       
        
            cout << "Doing average" << endl; 
            std::vector<Vec3d> a_sum, b_sum, c_sum, d_sum;
            
            if(init_id[0]) {
                a_sum.push_back(a0);
                b_sum.push_back(b0);
                c_sum.push_back(c0);
                d_sum.push_back(d0);
            }
            if(init_id[4]) {
                a_sum.push_back(a1);
                b_sum.push_back(b1);
                c_sum.push_back(c1);
                d_sum.push_back(d1);
            }
            if(init_id[0]||init_id[4]){
                for (int i=0; i<3; i++) {
                    a_avg[i] = 0.0;
                    b_avg[i] = 0.0;
                    c_avg[i] = 0.0;
                    d_avg[i] = 0.0;
                    for (unsigned int j=0; j<a_sum.size(); j++) {
                        a_avg[i] += a_sum[j][i];
                        b_avg[i] += b_sum[j][i];
                        c_avg[i] += c_sum[j][i];
                        d_avg[i] += d_sum[j][i];
                    }
                    a_avg[i] /= a_sum.size();
                    b_avg[i] /= b_sum.size();
                    c_avg[i] /= c_sum.size();
                    d_avg[i] /= d_sum.size();
                }
            }
            cout << a0[0] << a_avg[0] << b0[1] << b_avg[1] << b0[2] << b_avg[2] << endl;
	    
	    vector<vector<Point2d>> boxes1, boxes2, boxes3, boxes4;
	    vector<double> weights={0.9,0.1}; //weights for past and current frame

	    if(average==true) {
	        boxes1.push_back(box1);
	        boxes2.push_back(box2);
	        boxes3.push_back(box3);
	        boxes4.push_back(box4);
	    }
	    else {
		cout << "EMPTY!!!" << endl;
	    }
	    if (init_id[0] || init_id[4] ) {
		average = true;
                projectPoints(tvec3d, Vec3d::zeros(), a_avg, camMatrix, distCoeffs, tvec2d);
                projectPoints(box_cloud, Vec3d::zeros() , a_avg, camMatrix, distCoeffs, box1);
                projectPoints(box_cloud, Vec3d::zeros(), b_avg, camMatrix, distCoeffs, box2);
                projectPoints(box_cloud, Vec3d::zeros(), c_avg, camMatrix, distCoeffs, box3);
                projectPoints(box_cloud, Vec3d::zeros(), d_avg, camMatrix, distCoeffs, box4);
	    }
	    if(!boxes1.empty()) {
	        boxes1.push_back(box1);
	        boxes2.push_back(box2);
	        boxes3.push_back(box3);
	        boxes4.push_back(box4); 

	        box1 = avgBoxes(boxes1, weights);
	        box2 = avgBoxes(boxes2, weights);
	        box3 = avgBoxes(boxes3, weights);
	        box4 = avgBoxes(boxes4, weights);
	    }

	    	     
	    
           if(init_id[0]||init_id[4]) {
                DrawBox2D(imageCopy, box1, 60, 20, 220);
                DrawBox2D(imageCopy, box2, 0, 255, 0);
                DrawBox2D(imageCopy, box3, 0, 0, 255);
                DrawBox2D(imageCopy, box4, 255, 0, 0);
            }
        }
        else {
            for(unsigned int i=0; i<4; i++) {
                if(init_id[i*4]) {
                    t_lost[i] += delta_t;
                    if(t_lost[i] >= thr_lost) {
                        init_id[i*4] = init_id[i*4+1] = init_id[i*4+2] = init_id[i*4+3] = false;
                        t_lost[i] = 0;
			average=false;
                    }
                }
            }                    
            if(init_id[0]||init_id[4]) {
                DrawBox2D(imageCopy, box1, 60, 20, 220);
                DrawBox2D(imageCopy, box2, 0, 255, 0);
                DrawBox2D(imageCopy, box3, 0, 0, 255);
                DrawBox2D(imageCopy, box4, 255, 0, 0);
	    } 
        }   

        if(showRejected && rejected.size() > 0)
            aruco::drawDetectedMarkers(imageCopy, rejected, noArray(), Scalar(100, 0, 255));

        if (saveResults) cap.write(imageCopy);

        Mat imageResize;

        cv::resize(imageCopy, imageResize, Size(imageCopy.cols/3,imageCopy.rows/3));
        imshow("resize", imageResize);

        delta = ((double)getTickCount() - tickk) / getTickFrequency();
        delta_t = delta;


        cout << "Stable time " << t_stable[0] << endl;
        cout << t_stable[1] << endl;
        cout << t_stable[2] << endl;
        cout << t_stable[3] << endl;

        cout << "Lost time " << t_lost[0] << endl;
        cout << t_lost[1] << endl;
        cout << t_lost[2] << endl;
        cout << t_lost[3] << endl;

        cout << "///////////////////////////////////" << endl;


        char key = (char)waitKey(waitTime); 
        if(key == 27) break;
    }
    
    inputVideo.release();
    if (saveResults) cap.release();
    
    resultfile.close();

    return 0;
}
