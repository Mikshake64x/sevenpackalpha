#pragma once

#include <JuceHeader.h>

//==============================================================================
// Etiquette de version affichee apres le numero (v2.22.6 : "beta", demande du
// user : "c'est la beta donc 2.22.6 beta").
// Le numero reste celui du .jucer, purement numerique : il sert aux codes de
// version VST3 et au CFBundleVersion du Mac, qui refusent du texte.
// Chaine vide = version finale, on n'affiche que le numero.
// .github/workflows/build-mac.yml lit aussi cette ligne pour nommer le DMG/PKG.
#define SEVENPACK_BUILD_LABEL "beta"

// "2.22.6 beta" (ou "2.22.6" si l'etiquette est vide). Envoye aussi au serveur
// de licence comme plugin_version : /admin montre quelle build tourne chez qui.
inline juce::String sevenpackVersionLabel()
{
    const juce::String number (JucePlugin_VersionString);
    const juce::String label (SEVENPACK_BUILD_LABEL);
    return label.isEmpty() ? number : number + " " + label;
}
