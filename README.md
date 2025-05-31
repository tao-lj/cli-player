# cli-player - 技术文档

## 项目概述
本项目是一个基于C++开发的跨平台终端视频播放器，通过将视频帧实时转换为ASCII字符画并在终端显示，同时同步播放音频。核心特性包括：
- 纯命令行界面运行
- 自适应终端窗口大小
- 实时播放控制
- 音频/视频同步
- 跨平台支持（Windows/Linux）

## 技术架构
```plaintext
+-------------------+     +-----------------+     +-----------------+
|  视频解码模块      |     |  音频处理模块   |     |  终端渲染模块   |
|  (OpenCV)         |<--->|  (SDL_mixer)    |<--->|  (ANSI Escape)  |
+-------------------+     +-----------------+     +-----------------+
       |                         |                         |
+-------------------+     +-----------------+     +-----------------+
|  视频帧处理       |     |  音频播放       |     |  用户输入处理   |
|  (颜色转换/缩放)  |     |  (MP3解码)      |     |  (键盘监听)     |
+-------------------+     +-----------------+     +-----------------+
```

## 运行原理

### 1. 视频处理流水线
1. **帧捕获**：通过OpenCV的`VideoCapture`逐帧读取视频
2. **尺寸适配**：根据终端窗口大小计算最佳显示尺寸（保持宽高比）
3. **颜色量化**：使用双行像素合并技术（▄字符）：
   - 上像素作为背景色
   - 下像素作为前景色
4. **差异渲染**：
   - 计算相邻帧颜色差异：`Δ = sqrt(dR² + dG² + dB²)`
   - 仅重绘变化超过阈值(ε)的区域
5. **终端输出**：使用ANSI Escape Codes实现：
   ```cpp
   // 示例：设置光标位置和颜色
   printf("\33[%d;%dH\33[48;2;%d;%d;%dm\33[38;2;%d;%d;%dm▄", 
          row, col, R1, G1, B1, R2, G2, B2);
   ```

### 2. 音频同步机制
1. **音频提取**：通过FFmpeg分离音轨：
   ```bash
   ffmpeg -i input.mp4 -f mp3 -vn tmp.mp3
   ```
2. **SDL_mixer控制**：
   - 使用`Mix_PlayMusic`播放音频
   - 通过`Mix_SetMusicPosition`实现音画同步
3. **时间基准**：
   ```cpp
   // 音频时间轴为主时钟
   currentTime = std::chrono::duration<double>(now - startTime).count() - pauseTime;
   ```

### 3. 输入处理系统
- **非阻塞键盘监听**：
  - Windows：`_kbhit()` + `_getch()`
  - Linux：`poll()`系统调用
- **热键映射**：
  | 按键       | 功能             | 键值 |
  |------------|------------------|------|
  | 空格       | 暂停/恢复        | 32   |
  | ESC        | 退出             | 27   |
  | →          | 快进5秒         | 77   |
  | ←          | 快退5秒         | 75   |

## 核心功能实现

### 1. 自适应渲染优化
```cpp
void get_size() {
    // 获取终端尺寸
    #ifdef __linux__
        ioctl(STDIN_FILENO, TIOCGWINSZ, &size);
    #elif _WIN32
        GetConsoleScreenBufferInfo(hConsole, &csbi);
    #endif
    
    // 计算最佳显示尺寸（保持宽高比）
    float ratio = MIN(bufferWidth/videoWidth, bufferHeight/videoHeight);
    outSize = cv::Size(videoWidth*ratio, videoHeight*ratio);
}
```

### 2. 差异渲染算法
```cpp
bool cmp(cv::Vec3b x, cv::Vec3b y, int threshold) {
    int dr = x[2] - y[2];  // OpenCV使用BGR格式
    int dg = x[1] - y[1];
    int db = x[0] - y[0];
    return dr*dr + dg*dg + db*db > threshold*threshold;
}

// 在渲染循环中：
if (!cmp(newColor, lastColor, threshold)) {
    // 跳过重复像素的渲染
    continue; 
}
```

### 3. 音视频同步策略
```cpp
void play() {
    startTime = std::chrono::system_clock::now();
    while (true) {
        // 计算主时钟
        currentTime = get_elapsed_time() - pauseTime;
        
        // 同步音频位置
        if (!isPaused && fabs(Mix_GetMusicPosition() - currentTime) > 0.1) {
            Mix_SetMusicPosition(currentTime);
        }
        
        // 视频帧定位
        targetFrame = currentTime * videoFPS;
        video.set(CAP_PROP_POS_FRAMES, targetFrame);
    }
}
```

## 配置文件说明
配置文件`config.txt`示例：
```ini
# FFmpeg可执行文件路径
ffmpegPath = /usr/local/bin/ffmpeg

# 临时音频文件名
audioName = temp_audio.mp3

# 颜色差异指数（控制渲染灵敏度）
eps = 2.7

# 快进/快退间隔（秒）
skipInterval = 5.0
```

### 参数详解
| 参数名       | 类型   | 默认值      | 说明                                                                 |
|--------------|--------|-------------|----------------------------------------------------------------------|
| ffmpegPath   | string | "ffmpeg"    | FFmpeg可执行文件的完整路径                                          |
| audioName    | string | "tmp.mp3"   | 生成的临时音频文件名                                                |
| eps          | double | 2.7         | 颜色差异指数：值越大，重绘频率越低                                  |
| skipInterval | double | 5.0         | 快进/快退的时间跨度（单位：秒）                                     |

## 使用示例
```bash
./cli-player video.mp4
```

## 性能优化策略
1. **差异渲染**：仅更新变化的像素区域
2. **颜色缓存**：记录上一帧颜色数据
3. **批量绘制**：使用ANSI Escape序列合并绘制指令
4. **动态阈值**：根据丢帧数自动调整颜色敏感度
   ```cpp
   int threshold = MIN(pow(lost_frames, eps), 128);
   ```

## 跨平台实现细节

### Windows特定处理
```cpp
// 启用VT100转义序列
SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

// 非阻塞输入
while (!_kbhit()) {
    // 渲染循环...
}
```

### Linux特定处理
```cpp
// 设置原始终端模式
tcsetattr(STDIN_FILENO, TCSANOW, &newt);

// 窗口大小改变信号处理
signal(SIGWINCH, handle_winch);
```

## 已知限制与改进方向
1. **当前限制**：
   - 音频格式依赖FFmpeg的MP3编码
   - 控制台字体宽高比要在1:2左右
   - 高分辨率视频性能下降明显
   - 缺乏音量控制功能

2. **未来改进**：
   - 支持更多音频格式（通过SDL_mixer插件）
   - 添加GPU加速渲染（OpenCL/CUDA）
   - 支持不同尺寸的字体
   - 实现播放列表功能
   - 增加字幕支持

## 故障排查
| 现象                  | 可能原因                  | 解决方案                         |
|-----------------------|---------------------------|----------------------------------|
| 黑屏无输出            | 终端不支持VT100           | 改用支持ANSI的终端（如Windows Terminal） |
| 音频不同步            | 系统时钟精度不足          | 使用音频时钟作为主基准           |
| 颜色显示异常          | 终端色彩配置错误          | 检查`$TERM`环境变量设置          |
| 无法读取视频          | FFmpeg路径错误            | 检查配置文件中的ffmpegPath       |
