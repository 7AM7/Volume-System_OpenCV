#include <opencv\cv.h>
#include <opencv\highgui.h>
#include <iostream>
#include <cmath>
#include <Windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
using namespace std;
using namespace cv;
#define blue  CV_RGB(0,0,255)
#define green CV_RGB(0,255,0)
#define red   CV_RGB(255,0,0)
#define white CV_RGB(255,255,255)
#define black CV_RGB(0,0,0)
#define yellow CV_RGB(255,255,0)
#define CV_RGB(r, g, b)  cvScalar((b), (g), (r), 0)
#pragma comment(lib, "winmm")
HWND g_hDlg = NULL;
HWND hDlg;
GUID g_guidMyContext = GUID_NULL;

bool ChangeVolume(double nVolume, bool bScalar,BOOL mMute,float pLevel)
{

	HRESULT hr = NULL;
	bool decibels = false;
	bool scalar = false;
	double newVolume = nVolume;
	CoInitialize(NULL);
	IMMDeviceEnumerator *deviceEnumerator = NULL;
	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
		__uuidof(IMMDeviceEnumerator), (LPVOID *)&deviceEnumerator);
	IMMDevice *defaultDevice = NULL;

	hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
	deviceEnumerator->Release();
	deviceEnumerator = NULL;

	IAudioEndpointVolume *endpointVolume = NULL;
	hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume),
		CLSCTX_INPROC_SERVER, NULL, (LPVOID *)&endpointVolume);
	defaultDevice->Release();
	defaultDevice = NULL;

	// -------------------------
	float currentVolume = 0;
	endpointVolume->GetMasterVolumeLevel(&currentVolume);
	//printf("Current volume in dB is: %f\n", currentVolume);
	endpointVolume->SetMute(mMute, &g_guidMyContext);
	hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
	//CString strCur=L"";
	//strCur.Format(L"%f",currentVolume);
	//AfxMessageBox(strCur);
	endpointVolume->GetMasterVolumeLevel(&pLevel);
	// printf("Current volume as a scalar is: %f\n", currentVolume);
	if (bScalar == false)
	{
		hr = endpointVolume->SetMasterVolumeLevel((float)newVolume, NULL);
	}
	else if (bScalar == true)
	{
		hr = endpointVolume->SetMasterVolumeLevelScalar((float)newVolume, NULL);
	}
	endpointVolume->Release();

	CoUninitialize();

	return FALSE;
}
void ClearScreen(IplImage* imgScribble, IplImage* imgDrawing)
{
	cvSet(imgScribble, black);
	cvSet(imgDrawing, white);
}

IplImage* GetThresholdedImage(IplImage* img, CvScalar& lowerBound, CvScalar& upperBound)
{
	// Convert the image into an HSV image
	IplImage* imgHSV = cvCreateImage(cvGetSize(img), 8, 3);
	cvCvtColor(img, imgHSV, CV_BGR2HSV);

	IplImage* imgThreshed = cvCreateImage(cvGetSize(img), 8, 1);

	cvInRangeS(imgHSV, lowerBound, upperBound, imgThreshed);

	cvReleaseImage(&imgHSV);
	return imgThreshed;
}

int main()
{
	// controls
	double area_limit = 700;
	CvScalar lowerBound = cvScalar(20, 100, 100);//cvScalar(20, 100, 100);  // yellow
	CvScalar upperBound = cvScalar(30, 255, 255);

	// defaults
	int lineThickness = 2;
	CvScalar lineColor = blue;

	//VideoCapture capture =0;
	CvCapture* capture = 0;
	capture = cvCaptureFromCAM(0);
	if (!capture)
	{
		cout << "Could not initialize capturing...\n";
		return -1;
	}

	// This image holds the "scribble" data...
	// the tracked positions of the pointer object
	IplImage* imgScribble = NULL;


	IplImage* imgColorPanel = 0;
	imgColorPanel = cvLoadImage("cvPaint.png", CV_LOAD_IMAGE_COLOR); // load the panel image. (This is a png image, not designed/included in the source code!) 
	if (!imgColorPanel)
	{
		cout << "cvPaint.png is not found !!! \n";
		return -1;
	}

	IplImage* imgDrawing = 0;
	imgDrawing = cvCreateImage(cvSize(cvQueryFrame(capture)->width, cvQueryFrame(capture)->height),
		cvQueryFrame(capture)->depth,     //Bit depth per channel
		3  //number of channels
	);
	cvSet(imgDrawing, white);

	CvFont font, fontbig;
	cvInitFont(&font, CV_FONT_HERSHEY_COMPLEX, 1, .6, 0, 2, CV_AA);
	cvInitFont(&fontbig, CV_FONT_HERSHEY_COMPLEX, 3, .6, 0, 3, CV_AA);

	int confirm_close = 10, confirm_clear = 20; // counters for clear and exit confirmation
	char buffer[50]; // buffer for cvPutText
	int image_num = 0; // to keep track of image numbers for saving
	int posX = 0;
	int posY = 0;
	double counter = 0.0;
	float level = 0.0;
	bool isMute = false;
	int mute = 20;
	while (true)
	{
		IplImage* frame = 0;
		frame = cvQueryFrame(capture);
		if (!frame)
			break;
		cvFlip(frame, NULL, 1); // flip the frame to overcome mirroring problem



								// If this is the first frame, we need to initialize it
		if (imgScribble == NULL)
			imgScribble = cvCreateImage(cvGetSize(frame), 8, 3);

		// Median filter to decrease the background noise
		cvSmooth(frame, frame,
			CV_MEDIAN,
			5, 5 //parameters for filter, in this case it is filter size
		);


		// Holds the thresholded image (tracked color -> white, the rest -> black)
		IplImage* imgThresh = GetThresholdedImage(frame, lowerBound, upperBound);

		// Calculate the moments to estimate the position of the object
		CvMoments *moments = (CvMoments*)malloc(sizeof(CvMoments));
		cvMoments(imgThresh, moments, 1);


		// The actual moment values
		double moment10 = cvGetSpatialMoment(moments, 1, 0);
		double moment01 = cvGetSpatialMoment(moments, 0, 1);
		double area = cvGetCentralMoment(moments, 0, 0);

		// Holding the last and current positions
		int lastX = posX;
		int lastY = posY;

		posX = 0;
		posY = 0;


		if (moment10 / area >= 0 && moment10 / area < 1280 && moment01 / area >= 0 && moment01 / area < 1280
			&& area>area_limit /* to control the limit */)
		{
			posX = moment10 / area;
			posY = moment01 / area;
		}

		CvPoint cvpoint = cvPoint(150, 30); // location of the text

		if (posX > 0 && posX  < 90 && posY > 0 && posY < 120) // exit
		{
				cvPutText(frame, "You Are Mute Now !", cvpoint, &font, red);
				ChangeVolume(counter, true, true, level);
				//isMute = true;
				//mute--;
				//Sleep(1000);
		}
		else if ( posX < 90 && posY > 130 && posY<235 ) // volume higher
		{
			sprintf(buffer, " Volume is : %1.0f", counter*100);
			
			cvPutText(frame, buffer, cvpoint, &font, red);
			counter+=0.01;
			ChangeVolume(counter, true,false,level);
			if (counter >= 1.0)
			{
				counter = 1.0;
				//cvPutText(frame, "Volume 100%  !", cvpoint, &font, red);
			}
		}
		else if ( posX < 90 && posY > 300 && posY < 335) // volume lower
		{
			sprintf(buffer, " Volume is : %1.0f", counter * 100);

			cvPutText(frame, buffer, cvpoint, &font, red);
			counter -= 0.01;
			ChangeVolume(counter, true, false, level);
			if (counter <= 0.0)
			{
				counter = 0.0;
				//cvPutText(frame, "Volume 100%  !", cvpoint, &font, red);
			}
		}
		else if (posX < 90 && posY > 400) 
		{
			cvPutText(frame, "AM7 ~", cvPoint(575, 420), &font, yellow);
		}
		//575 //380
		cout << "position = " << posX << "\t" <<  posY << "\t"<<endl;
		//cout << "moment = " << moment10 << "\t" <<  moment01 << "\n";
		//cout << "d->" << magnitude << endl;
		//cout << "area = " << area << endl;

		// Add the scribbling image and the frame...
		cvAdd(imgDrawing, imgScribble, imgDrawing);

		// Combine everything in frame
		cvAnd(frame, imgDrawing, frame);
		cvAnd(imgColorPanel, frame, frame);

		cvShowImage("Tester", imgThresh);
		cvShowImage("Volume System", frame);


		int c = cvWaitKey(30);
		if (c == 27)  //ESC key
			break;
		//else if(c==49) // 1 key
		cvReleaseImage(&imgThresh);
		delete moments;
	}

	cvReleaseCapture(&capture);
	cvReleaseImage(&imgColorPanel);
	cvReleaseImage(&imgScribble);

	return 0;
}
