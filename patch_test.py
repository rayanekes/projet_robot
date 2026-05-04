import sys
sys.path.insert(0, '/home/rayane/projet_robot/backend/src')
from tts_engine import XTTSEngine
from TTS.tts.configs.xtts_config import XttsConfig
from TTS.tts.models.xtts import Xtts
from TTS.tts.layers.xtts.gpt import GPT2InferenceModel as _GPT2
import torch
import os
import soundfile as sf
import numpy as np

# Apply cache_position patch
_orig_update = _GPT2._update_model_kwargs_for_generation
def _safe_update(self, outputs, model_kwargs, is_encoder_decoder=False, num_new_tokens=1, **kwargs):
    if "cache_position" not in model_kwargs:
        try:
            device = outputs.hidden_states[0].device
            model_kwargs["cache_position"] = torch.tensor([0], device=device)
        except Exception:
            model_kwargs["cache_position"] = torch.tensor([0], device="cpu")
    return _orig_update(self, outputs, model_kwargs, is_encoder_decoder=is_encoder_decoder, num_new_tokens=num_new_tokens, **kwargs)
_GPT2._update_model_kwargs_for_generation = _safe_update

tts_home = os.path.expanduser('~/.local/share/tts')
xtts_dir = os.path.join(tts_home, 'tts_models--multilingual--multi-dataset--xtts_v2')
config = XttsConfig()
config.load_json(os.path.join(xtts_dir, 'config.json'))

model = Xtts.init_from_config(config)
model.load_checkpoint(config, checkpoint_dir=xtts_dir, eval=True)
model.cuda()
# NO MANUAL .half() !!!

# We need a valid audio to prevent empty latent error
dummy_wav = '/home/rayane/projet_robot/backend/voices/rayane.wav'

with torch.no_grad(), torch.amp.autocast('cuda', dtype=torch.float16):
    latent, emb = model.get_conditioning_latents(
        audio_path=[dummy_wav],
        gpt_cond_len=6,
        max_ref_length=config.max_ref_len,
        sound_norm_refs=config.sound_norm_refs,
    )
    
    gen = model.inference_stream('Bonjour tout le monde.', 'fr', latent, emb, enable_text_splitting=True)
    try:
        chunks = []
        for chunk in gen:
            chunks.append(chunk)
        print('SUCCESS NO MANUAL HALF. Bytes:', len(b"".join(chunks)))
    except Exception as e:
        print('ERROR:', e)
