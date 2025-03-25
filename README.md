# JC4827W543_avimp3Cinepak

# 🎞 Convert MP4 to AVI (Cinepak + PCM_U8) using FFmpeg

This guide explains how to convert an MP4 video into an **AVI** file using the **Cinepak** video codec and **PCM U8** mono audio. Cinepak requires that both width and height be **multiples of 4**, so we will scale or pad the video to a final resolution of **480x272**.

---

## ✅ Basic FFmpeg Command

```bash
ffmpeg -i input.mp4 -c:a mp3 -c:v cinepak -q:v 10 -vf "fps=24,scale=480:272" output.avi

- `-i input.mp4`: Input video file  
- `-ac 1`: Convert audio to mono  
- `-c:a pcm_u8`: Use 8-bit unsigned PCM audio  
- `-c:v cinepak`: Use the Cinepak codec (great for retro devices)  
- `-q:v 10`: Set video quality (lower = better quality, Cinepak max is ~1–20)  
- `-vf "fps=24,scale=480:272"`: Scale to 480x272 and force 24 FPS  
- `output.avi`: Output AVI file  

