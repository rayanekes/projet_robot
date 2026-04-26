from PIL import Image, ImageDraw, ImageFont

WIDTH = 320
HEIGHT = 240

def create_face(filename, emotion, frame=1):
    # Fond sombre mais pas totalement noir
    bg_color = (20, 20, 30)
    img = Image.new('RGB', (WIDTH, HEIGHT), color=bg_color)
    draw = ImageDraw.Draw(img)

    # Paramètres de base selon l'émotion
    if emotion == "neutre":
        main_color = (0, 255, 255) # Cyan
        eye_w, eye_h = 40, 60
    elif emotion == "joie":
        main_color = (255, 200, 0) # Jaune vif
        eye_w, eye_h = 45, 45
    elif emotion == "triste":
        main_color = (100, 150, 255) # Bleu triste
        eye_w, eye_h = 40, 50
    elif emotion == "parle":
        main_color = (0, 255, 100) # Vert menthe
        eye_w, eye_h = 40, 60
    elif emotion == "reflexion":
        main_color = (255, 50, 255) # Magenta
        eye_w, eye_h = 50, 50

    # Gestion de l'animation (clignement ou action)
    if frame == 2 and emotion != "reflexion":
        eye_h = 10  # Yeux fermés
        offset_y = 25
    else:
        offset_y = 0

    left_eye = [80, 60 + offset_y, 80 + eye_w, 60 + eye_h + offset_y]
    right_eye = [200, 60 + offset_y, 200 + eye_w, 60 + eye_h + offset_y]

    # --- JOUES ROSES (pour le côté drôle/mignon) ---
    if frame == 1 and emotion in ["joie", "parle", "neutre"]:
        draw.ellipse([50, 110, 80, 140], fill=(255, 100, 150))
        draw.ellipse([240, 110, 270, 140], fill=(255, 100, 150))

    # --- DESSIN DES YEUX ET BOUCHE ---
    if emotion == "neutre":
        draw.rounded_rectangle(left_eye, fill=main_color, radius=10) # Bords arrondis
        draw.rounded_rectangle(right_eye, fill=main_color, radius=10)
        draw.line([100, 180, 220, 180], fill=main_color, width=12)

    elif emotion == "joie":
        if frame == 2:
            draw.line([70, 80, 130, 80], fill=main_color, width=15)
            draw.line([190, 80, 250, 80], fill=main_color, width=15)
        else:
            # Yeux en arc de cercle
            draw.arc([70, 50, 130, 110], start=180, end=360, fill=main_color, width=15)
            draw.arc([190, 50, 250, 110], start=180, end=360, fill=main_color, width=15)
        # Grosse bouche souriante
        draw.arc([100, 120, 220, 200], start=0, end=180, fill=main_color, width=20)

    elif emotion == "triste":
        draw.rounded_rectangle(left_eye, fill=main_color, radius=5)
        draw.rounded_rectangle(right_eye, fill=main_color, radius=5)
        # Larmes animées
        if frame == 2:
            draw.ellipse([90, 130, 110, 150], fill=(50, 200, 255))
            draw.ellipse([210, 130, 230, 150], fill=(50, 200, 255))
        draw.arc([100, 160, 220, 230], start=180, end=360, fill=main_color, width=15)

    elif emotion == "parle":
        draw.rounded_rectangle(left_eye, fill=main_color, radius=10)
        draw.rounded_rectangle(right_eye, fill=main_color, radius=10)
        # Bouche qui s'anime
        mouth_h = 10 if frame == 1 else 50
        draw.ellipse([120, 160, 200, 160 + mouth_h], fill=(255, 50, 50)) # Intérieur rouge

    elif emotion == "reflexion":
        # Yeux asymétriques pour le côté drôle/confus
        draw.ellipse([70, 50, 130, 110], fill=main_color) # Gros œil rond
        draw.line([200, 80, 250, 80], fill=main_color, width=15) # Œil plissé

        # Bouche tordue
        draw.line([100, 190, 160, 190], fill=main_color, width=10)
        draw.line([160, 190, 220, 170], fill=main_color, width=10)

        # Animation des symboles de réflexion au-dessus de la tête
        if frame == 2:
            draw.text((250, 20), "?", fill=(255, 255, 0), font=None, font_size=50)
            draw.ellipse([50, 30, 65, 45], fill=(255, 255, 255))
        else:
            draw.text((250, 10), "?", fill=(255, 200, 0), font=None, font_size=40)
            draw.ellipse([30, 50, 40, 60], fill=(200, 200, 200))

    print(f"Génération de {filename} ({emotion} frame {frame}) en 320x240...")
    img.save(filename, format='BMP')

if __name__ == "__main__":
    print("🎨 Générateur d'animations TFT Colorées (BMP 24-bits 320x240)")

    emotions = ["joie", "neutre", "triste", "parle", "reflexion"]
    for emo in emotions:
        create_face(f"{emo}_1.bmp", emo, frame=1)
        create_face(f"{emo}_2.bmp", emo, frame=2)

    print("✅ Terminé ! Copiez TOUS les fichiers .bmp à la racine de votre carte SD.")
