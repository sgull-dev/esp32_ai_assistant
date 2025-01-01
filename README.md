# Jarvis-Local
Firmware for local AI assistant hardware. Usage from a ESP32 microchip, with attached speakers and microphone. Server on desktop will prompt a llama3 model and the various voice models. 

## ESP32
The firmware runs on a five stage loop:
1. Wait for button press to start recording.
2. Record audio while the button is down.
3. Send the Audio.
4. Wait to receive audio.
5. Play Audio
The ESP32's integrated Wifi-module is used to make a connection over your local Wifi, using WebSockets.
## Server Code
The server runs from a python script, loading the models and opening the websocket for listening. The server starts running it's code when it has succesfully received the raw microphone data. First the audio is converted to text with a Speech-to-Text whisper model. The llama3 (or whatever you want to use) local LLM is prompted with the converted text. Finally the reply from the prompted AI is also converted to voice and sent back to the ESP32.

More on how the system is setup on my blog: https://www.techdebtblog.com/esp32ai
