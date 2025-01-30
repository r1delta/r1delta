#!/usr/bin/env python3
import argparse
import subprocess
import os
import shutil
from pathlib import Path

def get_wav_channels(wav_path):
    """Get number of channels in WAV file using ffprobe"""
    cmd = [
        'ffprobe', '-v', 'error',
        '-select_streams', 'a:0',
        '-show_entries', 'stream=channels',
        '-of', 'csv=p=0',
        wav_path
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Error getting channels for {wav_path}: {result.stderr}")
    return int(result.stdout.strip())

def encode_wav(wav_path, opus_bitrate='96', force=False):
    wav_path = Path(wav_path)
    base_name = wav_path.with_suffix('')
    
    try:
        channels = get_wav_channels(wav_path)
    except Exception as e:
        print(f"Skipping {wav_path}: {str(e)}")
        return

    # Check if all opus files already exist
    all_exist = all(base_name.with_suffix(f'.opus{i}').exists() for i in range(channels))
    if all_exist and not force:
        print(f"Skipping {wav_path} - all opus tracks exist")
        return

    print(f"Processing {wav_path} ({channels} channels)...")

    # Create temp directory for channel splits
    temp_dir = Path(os.environ.get('TMP', '/tmp')) / f'opus_enc_{os.getpid()}'
    temp_dir.mkdir(exist_ok=True)

    try:
        # Split to mono channels and encode
        for channel in range(channels):
            temp_wav = temp_dir / f'channel_{channel}.wav'
            opus_path = base_name.with_suffix(f'.opus{channel}')

            # Extract single channel using ffmpeg
            ffmpeg_cmd = [
                'ffmpeg', '-y', '-v', 'error',
                '-i', str(wav_path),
                '-af', f'pan=mono|c0=c{channel}',
                '-ar', '48000',  # Opus prefers 48kHz
                str(temp_wav)
            ]
            subprocess.run(ffmpeg_cmd, check=True)

            # Encode to opus
            opusenc_cmd = [
                'opusenc',
                '--bitrate', opus_bitrate,
                '--quiet',
                str(temp_wav),
                str(opus_path)
            ]
            subprocess.run(opusenc_cmd, check=True)

            print(f"Created {opus_path}")

    finally:
        # Cleanup temp files
        shutil.rmtree(temp_dir, ignore_errors=True)

def process_directory(root_dir, bitrate, force=False):
    for path in Path(root_dir).rglob('*.wav'):
        try:
            encode_wav(path, opus_bitrate=bitrate, force=force)
        except subprocess.CalledProcessError as e:
            print(f"Failed to process {path}: {str(e)}")
        except KeyboardInterrupt:
            print("\nAborted by user")
            return

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Recursively encode WAV files to multi-track Opus',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('directory', help='Root directory containing WAV files')
    parser.add_argument('-b', '--bitrate', default='96', 
                      help='Opus encoding bitrate per channel (kbps)')
    parser.add_argument('-f', '--force', action='store_true',
                      help='Re-encode existing Opus files')
    args = parser.parse_args()

    # Check for required tools
    required_tools = ['ffprobe', 'ffmpeg', 'opusenc']
    for tool in required_tools:
        if not shutil.which(tool):
            print(f"Error: Required tool '{tool}' not found in PATH")
            exit(1)

    process_directory(args.directory, args.bitrate, args.force)
    print("Done!")
