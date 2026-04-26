#!/bin/bash

# Script de mise à jour automatique du projet Robot Assistant IA
# Permet de récupérer le dernier code sans se soucier du nom compliqué de la branche Git.

echo "🔄 Démarrage de la mise à jour du projet..."

# 1. On récupère les informations du serveur distant (GitHub) sans rien modifier
git fetch --all

# 2. On trouve le nom de la branche actuelle (ex: project-restructure-531404964...)
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo "📂 Branche actuelle détectée : $CURRENT_BRANCH"

# 3. On tire (pull) le dernier code de cette branche exacte depuis 'origin'
echo "📥 Téléchargement des dernières modifications..."
git pull origin $CURRENT_BRANCH

# 4. Vérification si ça a réussi
if [ $? -eq 0 ]; then
    echo "✅ Mise à jour réussie ! Le code est prêt à être utilisé."
    echo "▶️ Pour lancer le test : python backend/test/test_pipeline.py"
    echo "▶️ Pour lancer le serveur : python backend/src/server.py"
else
    echo "❌ Erreur lors de la mise à jour."
fi
