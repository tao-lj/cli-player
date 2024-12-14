#include <opencv2/opencv.hpp>
#include <SDL.h>
#include <windows.h>
#include <conio.h>
#include <fstream>

#define SPACE	32
#define ESC	27

#undef main
#undef max

using namespace cv;
using namespace std;
using namespace chrono;

double exponential = 2.7;		// ���ƶ�֡ʱ��ֵ��ָ������
string ffmpegPath = "ffmpeg";		// ffmpeg·��
string tmpAudioFile = "TmpAudio.wav";	// ��ʱ��Ƶ�ļ�����

VideoCapture video;

SDL_AudioDeviceID deviceId;	// ��Ƶ�豸 ID
SDL_AudioSpec wavSpec;		// ��Ƶ���
Uint8* wavBuffer;		// ��Ƶ������
Uint32 wavLength;		// ��Ƶ����

bool isPaused = false;
double currentTime, pausedTime;

// ��ȡ�����ļ�
void get_cfg() {
	ifstream cfg("config.txt");
	string str;

	// �������ļ��������򴴽�
	if (!cfg.is_open()) {
		cout << "Warning: �޷��������ļ�config.txt" << endl;
		ofstream fp("config.txt");
		fp << "ffmpegPath=" << ffmpegPath << endl;
		fp << "exponential=" << exponential;
		return;
	}

	// ���л�ȡ����ѡ��
	while (getline(cfg, str)) {
		auto a = str.find("=");
		if (a == string::npos) continue;
		string key = str.substr(0, a);
		string value = str.substr(a + 1, str.size() - 1);

		if (key == "ffmpegPath") ffmpegPath = value;
		else if (key == "exponential") exponential = stod(value);
	}
}

void init() {
	get_cfg();

	// ���ÿ���̨�����ն�����
	DWORD dwMode = 0;
	GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dwMode);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

	// ��ʼ�� SDL2 ���ڲ�����Ƶ
	if (SDL_Init(SDL_INIT_AUDIO) < 0) {
		cerr << "ERROR: SDL ��ʼ��ʧ��: " << SDL_GetError() << endl;
		exit(1);
	}
}

void get_file(int argc, char* argv[]) {
	// ��ȡ��Ƶ�ļ�·��
	string videoName = "";
	if (argc < 2) {
		cout << "��������Ƶ·����";
		getline(cin, videoName);
	}
	else videoName += argv[1];

	if (videoName[0] == '\"') {
		videoName.erase(videoName.end() - 1);
		videoName.erase(videoName.begin());
	}

	// ����Ƶ�ļ�
	if (!video.open(videoName)) {
		cerr << "ERROR: �޷�����Ƶ�ļ�" << endl;
		exit(1);
	}

	// ��ȡ��Ƶ������Ϊ WAV �ļ�
	string command = ffmpegPath + " -i \"" + videoName + "\" -f wav -vn " + tmpAudioFile;
	if (system(command.c_str()) != 0) { // ��� ffmpeg �����Ƿ�ִ�гɹ�
		cerr << "ERROR: ��Ƶ��ȡʧ��" << endl;
		video.release();
		exit(1);
	}
}

// ������Ƶ
void play_audio() {
	// ������Ƶ�ļ���������
	if (SDL_LoadWAV(tmpAudioFile.c_str(), &wavSpec, &wavBuffer, &wavLength) == NULL) {
		cerr << "ERROR: �޷�������Ƶ�ļ�: " << SDL_GetError() << endl;
		SDL_Quit();
		exit(1);
	}

	// ����Ƶ�豸
	deviceId = SDL_OpenAudioDevice(NULL, 0, &wavSpec, NULL, 0);
	if (deviceId == 0) {
		cerr << "ERROR: �޷�����Ƶ�豸: " << SDL_GetError() << endl;
		SDL_FreeWAV(wavBuffer);
		SDL_Quit();
		exit(1);
	}

	// ��ʼ������Ƶ
	SDL_QueueAudio(deviceId, wavBuffer, wavLength);
	SDL_PauseAudioDevice(deviceId, 0);
}

// ֹͣ��Ƶ
void stop_audio() {
	SDL_CloseAudioDevice(deviceId);	// �ر���Ƶ�豸
}

// �����ַ����ߴ�
void get_size(int& width, int& height) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	width = csbi.srWindow.Right - csbi.srWindow.Left << 1;
	height = csbi.srWindow.Bottom - csbi.srWindow.Top << 1;

	double aspectRatio = (double)csbi.dwSize.X / width / csbi.dwSize.Y * height;

	// �������߱�
	if ((double)video.get(CAP_PROP_FRAME_WIDTH) / width > aspectRatio * video.get(CAP_PROP_FRAME_HEIGHT) / height) {
		height = aspectRatio * width / video.get(CAP_PROP_FRAME_WIDTH) * video.get(CAP_PROP_FRAME_HEIGHT);
		height = height >> 1 << 1; // ȷ���߶�Ϊż��
	}
	else {
		width = (double)height / video.get(CAP_PROP_FRAME_HEIGHT) * video.get(CAP_PROP_FRAME_WIDTH) / aspectRatio;
	}
}

// ��ͣ/�����¼�����
void toggle_pause() {
	isPaused = !isPaused;
	SDL_PauseAudioDevice(deviceId, isPaused ? 1 : 0);
}

// ������Դ���˳�
void stop_and_exit() {
	stop_audio();
	puts("\33[0m\n\33[?25h\n\33c");
	
	remove(tmpAudioFile.c_str());
	video.release();
	SDL_FreeWAV(wavBuffer);
	SDL_Quit();
	
	exit(0);
}

// �����¼�����
void handle_keyboard() {
	if (_kbhit()) {
		char ch = _getch();
		switch (ch) {
		case SPACE:
			toggle_pause();
			break;
		case ESC:
			stop_and_exit();
		default:
			break;
		}
	}
}

void play() {
	int width, height, d, lost = 0, totalFrames = video.get(CAP_PROP_FRAME_COUNT);
	Mat img;
	Vec3b color, bgColor = { 0, 0, 0 }, txtColor = { 0, 0, 0 };
	auto startTime = high_resolution_clock::now();
	string output;

	// �ж���ɫ�����Ƿ񳬹���ֵ�������򷵻� true
	auto f = [&d](Vec3b x, Vec3b y) -> bool {
		return max({ abs(x[0] - y[0]), abs(x[1] - y[1]), abs(x[2] - y[2]) }) > d;
		};

	// ��ȡ��ǰ����ʱ�䲢����ת��Ϊ��Ƶ֡��
	auto getTime = [&]() -> double {
		currentTime = (duration<double>(high_resolution_clock::now() - startTime).count() - pausedTime);
		return currentTime * video.get(CAP_PROP_FPS);
		};

	puts("\33c\n\33[?25l\n\33[30;40m"); // ���������ع��
	play_audio();

	// ��Ƶ������ѭ��
	for (int idx = 0; idx < totalFrames; ++idx) {
		handle_keyboard();
		while (isPaused) {
			SDL_Delay(50);
			handle_keyboard();
			pausedTime = duration<double>(high_resolution_clock::now() - startTime).count() - currentTime;
		}

		Mat tmp;
		double current = getTime();
		if (!video.read(tmp)) break;

		if (current > idx) { // ������ʧ��֡
			++lost;
			continue;
		}
		while (current < idx) current = getTime(); // �ȴ�ͬ��

		get_size(width, height);

		Mat frame;
		bool firstFrame = 1, colorChanged = 1;
		d = pow(lost, exponential); // ���ݶ�֡��������ɫ������ֵ
		lost = 0;
		output.clear();
		resize(tmp, frame, { width, height }); // ����ͼ��ߴ�

		// ����ߴ�仯������
		if (img.cols != width || img.rows != height) {
			img = Mat(width, height, CV_8UC1, Scalar(0, 0, 0));
			puts("\33c");
		}

		for (int i = 0; i < height; i += 2) {
			for (int j = 0; j < width; ++j) {
				if (!f(frame.at<Vec3b>(i, j), img.at<Vec3b>(i, j)) && !f(frame.at<Vec3b>(i + 1, j), img.at<Vec3b>(i + 1, j))) {
					firstFrame = 1;
					continue; // �����Ļ����ɫ��ͼ����ɫ��ͬ���������
				}
				if (firstFrame) {
					firstFrame = colorChanged = 0;
					output += "\33[" + to_string(i / 2 + 1) + ';' + to_string(j + 1) + 'H'; // �ƶ����
				}
				color = frame.at<Vec3b>(i, j);
				if (f(color, bgColor)) { // �ı䱳����ɫ
					output += "\33[48;2;" + to_string(color[2]) + ';' + to_string(color[1]) + ';' + to_string(color[0]) + 'm';
					bgColor = color;
				}
				color = frame.at<Vec3b>(i + 1, j);
				if (f(color, txtColor)) { // �ı��ı���ɫ
					output += "\33[38;2;" + to_string(color[2]) + ';' + to_string(color[1]) + ';' + to_string(color[0]) + 'm';
					txtColor = color;
				}
				output += "�{"; // ����ַ�
			}
			if (!firstFrame && i + 2 < height) output += '\n';
		}
		if (!colorChanged) fputs(output.c_str(), stdout); // ��ӡ���
		img = frame; // ����ͼ��
	}

	stop_audio();
	puts("\33[0m\n\33[?25h\n\33c"); // �ָ��ն�����

	return;
}

int main(int argc, char* argv[]) {
	init();

	get_file(argc, argv);

	play();
	
	remove(tmpAudioFile.c_str());
	video.release();
	SDL_FreeWAV(wavBuffer);
	SDL_Quit();

	return 0;
}