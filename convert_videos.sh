#!/bin/bash

# FFmpeg script to convert videos for ESP32 AVI Player
# This script converts videos to Cinepak AVI format compatible with the ESP32 player

# Check if ffmpeg is installed
if ! command -v ffmpeg &> /dev/null; then
    echo "Error: ffmpeg is not installed. Please install it first:"
    echo "  macOS: brew install ffmpeg"
    echo "  Ubuntu/Debian: sudo apt install ffmpeg"
    echo "  Windows: Download from https://ffmpeg.org/download.html"
    exit 1
fi

# Create output directory if it doesn't exist
mkdir -p converted_videos

# Function to convert a single video
convert_video() {
    local input_file="$1"
    local filename=$(basename "$input_file")
    local name="${filename%.*}"
    local output_file="converted_videos/${name}.avi"
    
    echo "Converting: $input_file"
    echo "Output: $output_file"
    
    ffmpeg -i "$input_file" \
        -c:v cinepak \
        -c:a pcm_s16le \
        -r 15 \
        -s 480x272 \
        -b:v 500k \
        -b:a 128k \
        -ar 22050 \
        -ac 2 \
        -y \
        "$output_file"
    
    if [ $? -eq 0 ]; then
        echo "✅ Successfully converted: $output_file"
    else
        echo "❌ Failed to convert: $input_file"
    fi
    echo "---"
}

# Check if input files are provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <video_file1> [video_file2] ..."
    echo "Example: $0 *.mp4"
    echo "Example: $0 video1.mp4 video2.mov video3.avi"
    exit 1
fi

echo "🎬 ESP32 AVI Player Video Converter"
echo "=================================="
echo "Converting videos to Cinepak AVI format..."
echo "Target resolution: 480x272"
echo "Frame rate: 15 fps"
echo "Video bitrate: 500k"
echo "Audio: PCM 16-bit, 22.05kHz, stereo"
echo ""

# Convert each input file
for input_file in "$@"; do
    if [ -f "$input_file" ]; then
        convert_video "$input_file"
    else
        echo "❌ File not found: $input_file"
    fi
done

echo ""
echo "🎉 Conversion complete!"
echo "Copy the converted videos from 'converted_videos/' folder to your SD card's '/avi' folder"
