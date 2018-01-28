#include <jni.h>
#include <string>

#include <AR/ar.h>
#include <AR/arMulti.h>
#include <AR/video.h>
#include <AR/arFilterTransMat.h>
#include <AR2/tracking.h>
#include <KPM/kpm.h>
#include "ARMarkerNFT.hpp"

#include "ar_toolkit.hpp"
#include "utility.hpp"

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/calib3d/calib3d.hpp>

using namespace std;
using namespace cv;

// Logging macros
#define  LOG_TAG    "ARMovieNative"
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)


//=========Functions===========
static bool initDetection();
static void runDetection(Mat &input_mat);

//====Configuration Value=====
const char *cparam_name ;
char vconf[] = "";
const char markerConfigDataFilename[] = "/data/user/0/com.example.anirudh.android_opencv_template/cache/Data/markers.dat";

ARParam			cparam;
int				xsize = 320 , ysize = 320;
AR_PIXEL_FORMAT pixFormat  = AR_PIXEL_FORMAT_NV21 ;

//===============================
extern "C" {
JNIEXPORT jstring

JNICALL
Java_com_example_anirudh_android_1opencv_1template_ImageProcessing_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {

    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

JNIEXPORT void JNICALL
Java_com_example_anirudh_android_1opencv_1template_ImageProcessing_initializeAR(
        JNIEnv *env, jobject /* this */, jstring cam_para_path, jstring marker_path) {


    cparam_name = env->GetStringUTFChars( cam_para_path, NULL);
    //markerConfigDataFilename = env->GetStringUTFChars( marker_path, NULL);

    __android_log_write(ANDROID_LOG_ERROR, "Tag #############", cparam_name);
    __android_log_write(ANDROID_LOG_ERROR, "Tag #############", markerConfigDataFilename);

    initDetection();
}

JNIEXPORT void JNICALL
Java_com_example_anirudh_android_1opencv_1template_ImageProcessing_runDetection(
        JNIEnv *env, jobject /* this */, jlong in_mat) {

    Mat *inputMat = (Mat *) (in_mat);
    runDetection(*inputMat);
}

static bool initDetection() {
    // AR init.  ========================================

    //Load camera parameter data
    if (arParamLoad(cparam_name, 1, &cparam) < 0) {
        LOGI("setupCamera(): Error loading parameter file %s for camera.\n", cparam_name);
        arVideoClose();
        return (FALSE);
    }

    //Resize the frame
    if (cparam.xsize != xsize || cparam.ysize != ysize) {
        LOGI("*** Camera Parameter resized from %d, %d. ***\n", cparam.xsize, cparam.ysize);
        arParamChangeSize(&cparam, xsize, ysize, &cparam);
    }

    // Prints the camera parameter
    LOGI("*** Camera Parameter ***\n");
    arParamDisp(&cparam);

    //Create a lookuptable from the parameters
    if ((gCparamLT = arParamLTCreate(&cparam, AR_PARAM_LT_DEFAULT_OFFSET)) == NULL) {
        LOGI("Creates a lookup table.\n", cparam_name);
    }


    //Initializing NFT
    if (!initNFT(gCparamLT, pixFormat)) {
        LOGI("*** NFT Init error ***\n");
    }

    // Markers setup. ========================================

    // Load marker(s).
    newMarkers(markerConfigDataFilename, &markersNFT, &markersNFTCount);
    if (!markersNFTCount) {
        LOGI("Error loading markers from config. file '%s'.\n", markerConfigDataFilename);
        cleanup();
        exit(-1);
    }
    LOGI("Marker count = %d\n", markersNFTCount);

     //Marker data has been loaded, so now load NFT data.
    if (!loadNFTData()) {
        LOGI("Error loading NFT data.\n");
        cleanup();
        exit(-1);
    }

    return true;
}


static void runDetection(Mat &input_mat) {
    ARUint8 *gARTImage = NULL;
    gARTImage = (uchar *) input_mat.data;
    float trackingTrans[3][4] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    cv::Mat rot_vec = Mat::zeros(1, 3, CV_64F), trn_vec = Mat::zeros(1, 3,
                                                                     CV_64F), rot_mat = Mat::zeros(
            3, 3, CV_64F);


    if (true) {
        // NFT results.
        static int detectedPage = -2; // -2 Tracking not inited, -1 tracking inited OK, >= 0 tracking online on page.

        int i, j, k;

        // Grab a video frame.
        if (gARTImage != NULL) {

            gCallCountMarkerDetect++; // Increment ARToolKit FPS counter.

            // Run marker detection on frame
            if (threadHandle) {
                // Perform NFT tracking.
                float err;
                int ret;
                int pageNo;

                if (detectedPage == -2) {
                    trackingInitStart(threadHandle, gARTImage);
                    detectedPage = -1;
                }
                if (detectedPage == -1) {
                    ret = trackingInitGetResult(threadHandle, trackingTrans, &pageNo);
                    if (ret == 1) {
                        if (pageNo >= 0 && pageNo < surfaceSetCount) {
                            LOGI("Detected page %d.\n", pageNo);
                            detectedPage = pageNo;
                            ar2SetInitTrans(surfaceSet[detectedPage], trackingTrans);
                        } else {
                            LOGI("Detected bad page %d.\n", pageNo);
                            detectedPage = -2;
                        }
                    } else if (ret < 0) {
                        LOGI("No page detected.\n");
                        detectedPage = -2;
                    }
                }
                if (detectedPage >= 0 && detectedPage < surfaceSetCount) {
                    if (ar2Tracking(ar2Handle, surfaceSet[detectedPage], gARTImage, trackingTrans,
                                    &err) < 0) {
                        LOGI("Tracking lost.\n");
                        detectedPage = -2;
                    } else {
                        LOGI("Tracked page %d (max %d).\n", detectedPage, surfaceSetCount - 1);
                    }
                }
            } else {
                LOGI("Error: threadHandle\n");
                detectedPage = -2;
            }

            // Update markers.
            for (i = 0; i < markersNFTCount; i++) {
                markersNFT[i].validPrev = markersNFT[i].valid;
                if (markersNFT[i].pageNo >= 0 && markersNFT[i].pageNo == detectedPage) {
                    markersNFT[i].valid = TRUE;
                    for (j = 0; j < 3; j++)
                        for (k = 0; k < 4; k++) {
                            markersNFT[i].trans[j][k] = trackingTrans[j][k];
                        }

                } else markersNFT[i].valid = FALSE;


                if (markersNFT[i].valid) {

                    // Filter the pose estimate.
                    if (markersNFT[i].ftmi) {
                        if (arFilterTransMat(markersNFT[i].ftmi, markersNFT[i].trans,
                                             !markersNFT[i].validPrev) < 0) {
                            LOGI("arFilterTransMat error with marker %d.\n", i);
                        }
                    }

                    if (!markersNFT[i].validPrev) {
                        // Marker has become visible, tell any dependent objects.
                        // --->
                    }

                    // We have a new pose, so set that.
                    //arglCameraViewRH((const ARdouble (*)[4])markersNFT[i].trans, markersNFT[i].pose.T, VIEW_SCALEFACTOR);


                    //Print(markersNFT[i].pose.T);
                    PrintTransform(trackingTrans);

                    //************Rotation Matrix************

//                    rot_mat.at<float>(0,0) = trackingTrans[0][0];
//                    rot_mat.at<float>(0,1) = trackingTrans[0][1];
//                    rot_mat.at<float>(0,2) = trackingTrans[0][2];
//                    rot_mat.at<float>(1,0) = trackingTrans[1][0];
//                    rot_mat.at<float>(1,1) = trackingTrans[1][1];
//                    rot_mat.at<float>(1,2) = trackingTrans[1][2];
//                    rot_mat.at<float>(2,0) = trackingTrans[2][0];
//                    rot_mat.at<float>(2,1) = trackingTrans[2][1];
//                    rot_mat.at<float>(2,2) = trackingTrans[2][2];

                    rot_mat.at<float>(0, 0) = trackingTrans[0][0];
                    rot_mat.at<float>(0, 1) = trackingTrans[0][0];
                    rot_mat.at<float>(0, 2) = trackingTrans[0][0];
                    rot_mat.at<float>(1, 0) = trackingTrans[0][0];
                    rot_mat.at<float>(1, 1) = trackingTrans[0][0];
                    rot_mat.at<float>(1, 2) = trackingTrans[0][0];
                    rot_mat.at<float>(2, 0) = trackingTrans[0][0];
                    rot_mat.at<float>(2, 1) = trackingTrans[0][0];
                    rot_mat.at<float>(2, 2) = trackingTrans[0][0];

                    Rodrigues(rot_mat, rot_vec);

                    //************Translation Matrix***********
                    trn_vec.at<double>(0, 0) = trackingTrans[0][3];
                    trn_vec.at<double>(0, 1) = trackingTrans[1][3];
                    trn_vec.at<double>(0, 2) = trackingTrans[2][3];

                    cout << trn_vec << endl;

                    //************Camera Matrix*****************
                    cv::Mat intrisicMat(3, 3, cv::DataType<double>::type); // Intrisic matrix
                    intrisicMat.at<double>(0, 0) = 674.171631;
                    intrisicMat.at<double>(1, 0) = 0;
                    intrisicMat.at<double>(2, 0) = 0;

                    intrisicMat.at<double>(0, 1) = 0;
                    intrisicMat.at<double>(1, 1) = 633.898087;
                    intrisicMat.at<double>(2, 1) = 0;

                    intrisicMat.at<double>(0, 2) = 318.297791;
                    intrisicMat.at<double>(1, 2) = 237.900467;
                    intrisicMat.at<double>(2, 2) = 1;

                    //************Dist Matrix**********************
                    cv::Mat distCoeffs(5, 1, cv::DataType<double>::type);   // Distortion vector
                    distCoeffs.at<double>(0) = 0.1147807688;
                    distCoeffs.at<double>(1) = -0.5208189487;
                    distCoeffs.at<double>(2) = -0.0002069871;
                    distCoeffs.at<double>(3) = -0.0040593124;
                    distCoeffs.at<double>(4) = 0;


                    std::vector<cv::Point3d> inputPnt;
                    std::vector<cv::Point2d> outPnt;

                    cv::Point3d pnt = Point3d(0, 0, 0);
                    cout << pnt << endl;

                    //cv::Point3d pnt = Point3d(markersNFT[i].pose.T[12],markersNFT[i].pose.T[13],markersNFT[i].pose.T[14]);

                    inputPnt.push_back(pnt);

                    cv::projectPoints(inputPnt, rot_vec, trn_vec, intrisicMat, distCoeffs, outPnt);
                    cout << outPnt << endl;

                    // Convert from ARToolkit Image type to OpenCV Mat
                    IplImage *iplImg;
                    iplImg = cvCreateImage(cvSize(640, 480), IPL_DEPTH_8U, 3);

                    // Get video frame from ARToolkit
                    iplImg->imageData = (char *) gARTImage;
                    input_mat = cv::cvarrToMat(iplImg);

                    //cout<<outPnt<<"**********"<<endl;
                    //****Draw a Circle using the pose data****
                    cv::circle(input_mat, Point(outPnt[0].x, outPnt[0].y), 30,
                               Scalar(255, 255, 255), 4);

                } else {

                    if (markersNFT[i].validPrev) {
                        // Marker has ceased to be visible, tell any dependent objects.
                        // --->
                    }
                }
            }
        }
    }
}
}
