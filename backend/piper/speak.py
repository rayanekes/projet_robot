import subprocess

def speak(text):
    p = subprocess.Popen(
        ["./piper/piper",
         "--model",
         "./piper/fr_FR-siwis-medium.onnx",
         "--output_raw"],
        stdin=subprocess.PIPE
    )
    p.communicate(text.encode())

speak("Test de synthèse vocale en temps réel.")
