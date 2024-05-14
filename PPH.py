### PyPromptHandler
# Functions:
# -> listen to TCP socket, receive audio files from ESP32
# Upon receiving a file:
#   -> (convert to mp3 with ffmpeg)
#   -> run audio through speech-to-text
#   -> prompt llama3 through ollama API with the text prompt
#   -> run text result through text-to-speech model
#   -> send audio back to ESP32

import socket
import requests
import json
import wave
from io import BytesIO

import numpy as np
from deepspeech import Model
from espeakng import ESpeakNG

from pydub import AudioSegment
from pydub.playback import play


def receive_file(client_socket):
    with open("rec_audio.raw", "wb") as f:
        while True:
            data = client_socket.recv(1024)
            if not data:
                break
            f.write(data)
    return

def stt(path):
    #take file at path and convert to String
    #First load audio to int16 array
    num_channels = 1
    sample_rate = 16000 
    dtype = np.int16
    with open(path, 'rb') as f:
        data = np.fromfile(f, dtype=dtype)
    #Load Speech inference model
    ds = Model("deepspeech/deepspeech-0.9.3-models.pbmm")
    ds.enableExternalScorer("deepspeech/deepspeech-0.9.3-models.scorer")
    #Return infered string
    return ds.stt(data)

# TODO: Obvious problem with this is it's always starting a new convo and not continuing on same
#       Fine for now, but if good to keep in mind for future develeopment
def prompt_llama(s):
    #take string and pass it to ollama as prompt
    url = 'http://localhost:11434/api/generate'
    data = {
        "model": "llama3",
        "prompt": s,
        "stream": False
    }
    json_data = json.dumps(data)

    response = requests.post(url, data=json_data)

    if response.status_code == 200:
        # Parse JSON response
        res_obj = response.json()
        # Return string output from the parsed JSON object, assuming it has a key named 'response'
        return res_obj.get('response')  # Ensure the key matches the expected output format
    else:
        # Handle errors or unexpected response statuses
        return f"Error: Received status code {response.status_code}"

def tts(s):
    #Convert string to audio
    esng = ESpeakNG()
    esng.voice = 'en-us'
    wavs = esng.synth_wav(s)
    audio_seg = AudioSegment.from_file(BytesIO(wavs), format="wav")
    audio_seg.set_frame_rate(16000)
    audio_seg.set_channels(1)
    wavIO = BytesIO()
    audio_seg.export(wavIO, format="wav")

    #Save to file for debug
    with open("audio69.wav", "wb") as f:
        f.write(wavIO.getvalue())
    wavIO.seek(0)
    return wavIO.getvalue()

def handle_playing_audio():
    text = stt("rec_audio.raw")
    res = prompt_llama(text)
    res_cut = ""
    i = 0
    for c in res:
        res_cut += c
        i = i+1
        if (i > 400):
            break
    print("Response is: ")
    print(res_cut)
    raw = tts(res_cut)
    stream_audio(raw)

def loop_receive_server():
    #Init socket for receiving files
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind(('192.168.1.139', 7998))
    server_socket.listen(5)
    print("Listening for incoming connections...")
    
    #Loop receive socket to get files always in :D
    try:
        while True:
            client_socket, addr = server_socket.accept()
            print(f"Connection from {addr} has been established.")
            
            # Handle client in a separate function or thread
            receive_file(client_socket)
            handle_playing_audio()
            
            client_socket.close()
            print("Connection closed.")
    except KeyboardInterrupt:
        print("Server is shutting down.")
    finally:
        server_socket.close()
        print("Server has been closed.")


def stream_audio(wav_data):
    host_ip = '192.168.1.139'
    port = 7999

    server_socket = socket.socket()
    server_socket.bind((host_ip, port))
    server_socket.listen(5)

    print('Server listening at', (host_ip, port))
    
    CHUNK_SIZE = 1024
    while True:
        client_socket, addr = server_socket.accept()
        print(f"Connection from {addr} has been established.")


        for i in range(0, len(wav_data), 10*CHUNK_SIZE):
            chunk = wav_data[i:i+10*CHUNK_SIZE]
            client_socket.sendall(chunk)

        client_socket.close()


if __name__ == "__main__":
    max_chars = 400
    loop_receive_server()
    
    # DO Unit Tests here lmao:
    # Speech-2-Text:
    #text = stt("audio3.raw")
    #res = prompt_llama(text)
    #res2 = ""
    #i = 0
    #for c in res:
    #    res2 += c
    #    i = i+1
    #    if (i >= max_chars):
    #        break
    #raw = tts(res2)
    #play(raw)
    
    #print(text)

    
    # Prompting Llama3:
    #text2 = prompt_llama("Explain what books are?")
    #print(text2)

    #raw = tts("Do you like bananas? Bananas are a yellow fruit. ww")
    #play_tts_audio()
    #stream_audio(raw)
    #text = "How long does this make? Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum"
    #tts_raw = tts(text)

def dont_call_me():
    # E2E testing:
    print("Attempting to prompt llama3")
    text3 = prompt_llama("Explain what are bananas")
    print("Cutting off too many chars")
    res = ""
    i = 0
    for c in text3:
        res += c
        i = i+1
        if (i >= max_chars):
            break
    print("Prompted llama, sending to TTS")
    raw = tts(res)
    print("TTS:sed prompt output")
    print("Sending audio to esp32")
    stream_audio(raw)
    
