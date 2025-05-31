# cli-player

## 项目概述
本项目是一个基于C++开发的跨平台终端视频播放器，通过将视频帧实时转换为ASCII字符画并在终端显示，同时同步播放音频。核心特性包括：
- 纯命令行界面运行
- 自适应终端窗口大小
- 实时播放控制
- 音频/视频同步
- 跨平台支持（Windows/Linux）


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

