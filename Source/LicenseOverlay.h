#pragma once

#include <JuceHeader.h>
#include "LicenseManager.h"
#include "AlphaSkin.h"
#include "DiscordLogo.h"

// Full-window modal blocking the plugin UI until a valid license is activated.
// Sits as the last child of the editor so it paints on top and eats all clicks.
// Can also be opened manually (see showManually()) from the About screen's
// "License" link or the Options gear — same activation form, but with a
// Close button available whenever the current state is usable (licensed or
// still within the demo window), so it never traps someone mid-demo.
class LicenseOverlay : public juce::Component, private juce::Timer
{
public:
    explicit LicenseOverlay (LicenseManager& lm) : license (lm)
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText ("Activate Sevenpack Alpha", juce::dontSendNotification);
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setFont (juce::Font (20.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, AlphaPalette::get().heading());

        addAndMakeVisible (keyEditor);
        // v2.23.2 : plus "(required)" -- la cle ne sert qu'a Activate (la demo
        // et le programme beta n'en demandent pas).
        keyEditor.setTextToShowWhenEmpty ("License key", juce::Colours::grey);
        keyEditor.setJustification (juce::Justification::centred);
        keyEditor.onReturnKey = [this] { activate(); };

        addAndMakeVisible (firstNameEditor);
        firstNameEditor.setTextToShowWhenEmpty ("First name (optional)", juce::Colours::grey);
        firstNameEditor.setJustification (juce::Justification::centred);
        firstNameEditor.onReturnKey = [this] { submit(); };

        addAndMakeVisible (lastNameEditor);
        lastNameEditor.setTextToShowWhenEmpty ("Last name (optional)", juce::Colours::grey);
        lastNameEditor.setJustification (juce::Justification::centred);
        lastNameEditor.onReturnKey = [this] { submit(); };

        addAndMakeVisible (emailEditor);
        emailEditor.setTextToShowWhenEmpty ("Email (required)", juce::Colours::grey);
        emailEditor.setJustification (juce::Justification::centred);
        emailEditor.onReturnKey = [this] { submit(); };

        addAndMakeVisible (activateButton);
        activateButton.setButtonText ("Activate");
        activateButton.onClick = [this] { activate(); };

        addAndMakeVisible (demoButton);
        demoButton.setButtonText ("Start 14-Day Demo");
        demoButton.onClick = [this] { license.clearBetaJoinError(); license.startDemo(); };

        // Programme beta (v2.23.2, demande : "Il faut beta program, le systeme
        // de licences et le mode demo") : acces immediat jusqu'a la fin de la
        // beta, sans cle (voir LicenseManager::joinBeta()).
        // v2.23.3 : le bouton ouvre une VUE beta a part, sans champ de cle
        // ("join the beta program n'a pas besoin de licence"), avec les
        // conditions du programme et le lien Discord.
        addChildComponent (betaButton);
        betaButton.setButtonText ("Join the beta program");
        betaButton.onClick = [this] { setBetaMode (true); };

        addChildComponent (joinBetaButton);
        joinBetaButton.setButtonText ("Join the beta");
        joinBetaButton.onClick = [this] { joinBeta(); };

        addChildComponent (backButton);
        backButton.setButtonText ("Back");
        backButton.onClick = [this] { setBetaMode (false); };

        addChildComponent (discordButton);
        discordButton.onClick = [] { juce::URL (kDiscordUrl).launchInDefaultBrowser(); };

        addChildComponent (closeButton);
        closeButton.setButtonText ("Close");
        closeButton.onClick = [this] { manuallyShown = false; updateFromStatus(); };

        addAndMakeVisible (statusLabel);
        statusLabel.setJustificationType (juce::Justification::centred);
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0402b));
        statusLabel.setFont (juce::Font (13.0f));

        setInterceptsMouseClicks (true, true);
        launchedAtMs = juce::Time::getMillisecondCounter();
        updateFromStatus();
        startTimerHz (2);
    }

    // Called when the user clicks "License" on the About screen or "License
    // Manager" in Options — shows the same activation form, plus a Close
    // button if the license is currently usable (so it's optional, not a wall).
    void showManually()
    {
        manuallyShown = true;
        updateFromStatus();
        toFront (true);
    }

    // Style Alpha : voile + panneau clair autour du formulaire.
    void paint (juce::Graphics& g) override
    {
        AlphaDraw::overlayVeil (g, getLocalBounds().toFloat());
        AlphaDraw::overlaySheet (g, getLocalBounds().withSizeKeepingCentre (380, betaMode ? 500 : 440).toFloat(), 12.0f); // v2.23.2 : +40 (ligne beta)

        if (betaMode)
        {
            // Conditions du programme beta (demande du user, texte en anglais).
            const auto pal = AlphaPalette::get();
            auto area = disclaimerArea;
            for (auto [text, bold] : { std::pair<const char*, bool> { kBetaIntro, true }, { kBetaTerms, false } })
            {
                juce::AttributedString as;
                as.setText (text);
                as.setFont (juce::Font (juce::FontOptions (13.0f, bold ? juce::Font::bold : juce::Font::plain)));
                as.setColour (bold ? pal.sheetText() : pal.sheetTextSoft());
                as.setJustification (juce::Justification::centredTop);
                as.setLineSpacing (1.5f);
                juce::TextLayout tl;
                tl.createLayout (as, area.getWidth());
                tl.draw (g, area.removeFromTop (tl.getHeight()));
                area.removeFromTop (8.0f);
            }
        }
    }

    void visibilityChanged() override
    {
        if (isVisible())
        {
            AlphaDraw::styleControls (*this);
            titleLabel.setColour (juce::Label::textColourId, AlphaPalette::get().heading());
            for (auto* e : { &keyEditor, &firstNameEditor, &lastNameEditor, &emailEditor })
                e->setTextToShowWhenEmpty (e->getTextToShowWhenEmpty(), AlphaPalette::get().sheetTextSoft());
        }
    }

    void resized() override
    {
        if (betaMode)
        {
            layoutBeta();
            return;
        }
        auto area = getLocalBounds().withSizeKeepingCentre (320, 376); // v2.22.6 : +20 (3 lignes de message) ; v2.23.2 : +36 (ligne beta)
        titleLabel.setBounds (area.removeFromTop (30));
        area.removeFromTop (16);
        keyEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (14);
        firstNameEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        lastNameEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        emailEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (14);

        auto buttonRow = area.removeFromTop (28);
        if (demoButton.isVisible())
        {
            auto buttons = buttonRow.withSizeKeepingCentre (280, 28);
            activateButton.setBounds (buttons.removeFromLeft (120));
            demoButton.setBounds (buttons.withTrimmedLeft (8));
        }
        else
        {
            activateButton.setBounds (buttonRow.withSizeKeepingCentre (120, 28));
        }
        if (betaButton.isVisible())
        {
            area.removeFromTop (8);
            betaButton.setBounds (area.removeFromTop (28).withSizeKeepingCentre (280, 28));
        }
        area.removeFromTop (10);
        statusLabel.setBounds (area.removeFromTop (58));

        if (closeButton.isVisible())
        {
            area.removeFromTop (6);
            closeButton.setBounds (area.removeFromTop (28).withSizeKeepingCentre (120, 28));
        }
    }

private:
    static constexpr const char* kDiscordUrl = "https://discord.gg/pHHWfqdEE";
    static constexpr const char* kBetaIntro  = "Free access until the end of the beta. No license key needed.";
    static constexpr const char* kBetaTerms  = "The beta program does not include a license for the released version. "
                                               "Side Key System reserves the right to change the duration of the beta program.";

    // Vue beta : titre, conditions, nom / e-mail, Back | Join the beta, Discord.
    void layoutBeta()
    {
        auto area = getLocalBounds().withSizeKeepingCentre (320, 436);
        titleLabel.setBounds (area.removeFromTop (30));
        area.removeFromTop (12);
        disclaimerArea = area.removeFromTop (104).toFloat();
        area.removeFromTop (10);
        firstNameEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        lastNameEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        emailEditor.setBounds (area.removeFromTop (28));
        area.removeFromTop (14);
        auto buttons = area.removeFromTop (28).withSizeKeepingCentre (280, 28);
        backButton.setBounds (buttons.removeFromLeft (100));
        joinBetaButton.setBounds (buttons.withTrimmedLeft (8));
        area.removeFromTop (14);
        discordButton.setBounds (area.removeFromTop (34).withSizeKeepingCentre (discordButton.getIdealWidth(), 34));
        area.removeFromTop (6);
        statusLabel.setBounds (area.removeFromTop (40));
    }

    void setBetaMode (bool on)
    {
        betaMode = on;
        license.clearBetaJoinError();
        statusLabel.setText ({}, juce::dontSendNotification);
        updateFromStatus();
        resized();
        repaint();
        if (on)
            emailEditor.grabKeyboardFocus();
    }

    void submit() { if (betaMode) joinBeta(); else activate(); }

    // Logo Discord (image de l'icone officielle, DiscordLogo.h) + texte : ouvre le
    // serveur Discord du programme beta.
    class DiscordButton : public juce::Button
    {
    public:
        DiscordButton() : juce::Button ("Discord")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip (kDiscordUrl);
            logo = juce::ImageCache::getFromMemory (DiscordLogo::png, DiscordLogo::pngSize);
        }

        int getIdealWidth() const
        {
            return 34 + 10 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (font(), kLabel)) + 4;
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto pal = AlphaPalette::get();
            auto b = getLocalBounds().toFloat();
            auto badge = b.removeFromLeft (34.0f).withSizeKeepingCentre (34.0f, 34.0f);
            g.setOpacity (down ? 0.85f : 1.0f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality); // 256 px -> 34 px, net
            g.drawImage (logo, badge, juce::RectanglePlacement::centred);
            g.setOpacity (1.0f);
            b.removeFromLeft (10.0f);
            g.setColour (over ? juce::Colour (0xff5865f2) : pal.sheetText());
            g.setFont (font());
            g.drawText (kLabel, b, juce::Justification::centredLeft, false);
            if (over)
                g.fillRect (b.getX(), b.getCentreY() + 8.0f, (float) juce::GlyphArrangement::getStringWidth (font(), kLabel), 1.0f);
        }

    private:
        static constexpr const char* kLabel = "Join the beta community on Discord";
        static juce::Font font() { return juce::Font (juce::FontOptions (13.0f, juce::Font::bold)); }
        juce::Image logo;
    };

    void activate()
    {
        auto key = keyEditor.getText().trim();
        auto email = emailEditor.getText().trim();
        if (key.isEmpty())
            return;

        if (email.isEmpty())
        {
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0402b));
            statusLabel.setText ("Email is required.", juce::dontSendNotification);
            return;
        }

        activateButton.setEnabled (false);
        license.clearBetaJoinError();
        statusLabel.setColour (juce::Label::textColourId, AlphaPalette::get().sheetText());
        statusLabel.setText ("Checking...", juce::dontSendNotification);
        license.activate (key, firstNameEditor.getText(), lastNameEditor.getText(), email);
    }

    void joinBeta()
    {
        const auto email = emailEditor.getText().trim();
        if (email.isEmpty() || ! email.containsChar ('@'))
        {
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0402b));
            statusLabel.setText ("Enter your email to join the beta program.", juce::dontSendNotification);
            return;
        }
        license.joinBeta (firstNameEditor.getText(), lastNameEditor.getText(), email);
        updateFromStatus();
    }

    // Message du programme beta (en cours / erreur), prioritaire sur le statut.
    bool showBetaJoinMessage()
    {
        if (license.isJoiningBeta())
        {
            statusLabel.setColour (juce::Label::textColourId, AlphaPalette::get().sheetText());
            statusLabel.setText ("Joining the beta program...", juce::dontSendNotification);
            return true;
        }
        const auto err = license.getBetaJoinError();
        if (err.isEmpty())
            return false;
        juce::String msg;
        if (err == "beta_closed" || err == "expired") msg = "The beta program has ended.";
        else if (err == "revoked")                    msg = "Your beta access has been revoked.";
        else if (err == "network_error")              msg = "Could not reach the license server.\nCheck your internet connection and try again.";
        else if (err == "no_beta_program")            msg = "No beta program is running for Sevenpack Alpha.";
        else                                          msg = "Could not join the beta program (" + err + ").";
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0402b));
        statusLabel.setText (msg, juce::dontSendNotification);
        return true;
    }

    void timerCallback() override { updateFromStatus(); }

    void updateFromStatus()
    {
        auto status = license.getStatus();

        // Usable (licensed or still within the demo) + opened manually means
        // the user is just here to check/change their license, not forced —
        // they can close without entering anything.
        bool canClose = manuallyShown && license.isUsable();

        // Don't auto-block the UI while a stored key is still being checked
        // against the server at startup ("activating") -- only pop up for an
        // actual problem (never activated, invalid, or trial ended). A slow
        // or offline check just leaves the plugin running normally until the
        // result comes back (licensed -> stays hidden, invalid -> then shows).
        bool isProblem = status == LicenseManager::Status::unlicensed
                          || status == LicenseManager::Status::invalid
                          || status == LicenseManager::Status::trialExpired
                          || status == LicenseManager::Status::betaEnded;

        // Grace window: the check runs silently in the background on every
        // launch -- never slam the overlay up the instant the editor opens.
        // Only surface it if the problem is still there after a few seconds,
        // so a normal (licensed/trial) launch never sees it flash at all.
        // v2.23.4 : seulement s'il y a une cle enregistree a verifier. Sans cle
        // (nouvel utilisateur, demo finie), rien a attendre du serveur : l'ecran
        // s'affiche tout de suite (avant : 8 s pendant lesquelles on pouvait
        // deja toucher au plugin).
        const bool hasStoredKey = license.getLicenseKey().isNotEmpty();
        bool graceElapsed = ! hasStoredKey || (juce::Time::getMillisecondCounter() - launchedAtMs) >= graceMs;

        setVisible (manuallyShown || (isProblem && graceElapsed));
        activateButton.setEnabled (status != LicenseManager::Status::activating);

        // Inscription reussie : on quitte la vue beta (licence beta active).
        if (betaMode && status == LicenseManager::Status::licensed)
        {
            betaMode = false;
            repaint();
            resized();
        }

        // Vue beta (v2.23.3) : pas de champ de cle ni de boutons de licence.
        for (juce::Component* c : { (juce::Component*) &joinBetaButton, (juce::Component*) &backButton, (juce::Component*) &discordButton })
            c->setVisible (betaMode);
        joinBetaButton.setEnabled (! license.isJoiningBeta());
        if (betaMode)
        {
            for (juce::Component* c : { (juce::Component*) &keyEditor, (juce::Component*) &activateButton, (juce::Component*) &demoButton,
                                        (juce::Component*) &betaButton, (juce::Component*) &closeButton })
                c->setVisible (false);
            titleLabel.setText ("Join the beta program", juce::dontSendNotification);
            if (! showBetaJoinMessage())
                statusLabel.setText ({}, juce::dontSendNotification);
            return;
        }
        keyEditor.setVisible (true);
        activateButton.setVisible (true);

        bool shouldShowDemoButton = license.canStartDemo();
        // Programme beta : propose tant qu'on n'a pas de licence (demo comprise).
        const bool shouldShowBetaButton = status == LicenseManager::Status::unlicensed
                                           || status == LicenseManager::Status::invalid
                                           || status == LicenseManager::Status::trial
                                           || status == LicenseManager::Status::trialExpired;
        bool layoutChanged = (shouldShowDemoButton != demoButton.isVisible())
                              || (shouldShowBetaButton != betaButton.isVisible())
                              || (closeButton.isVisible() != canClose);

        demoButton.setVisible (shouldShowDemoButton);
        betaButton.setVisible (shouldShowBetaButton);
        betaButton.setEnabled (! license.isJoiningBeta());
        closeButton.setVisible (canClose);

        if (status == LicenseManager::Status::licensed && license.isBetaLicense())
        {
            titleLabel.setText ("Sevenpack Alpha Beta", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, AlphaPalette::get().sheetText());
            statusLabel.setText ("Beta license, active until " + license.getBetaEndDateText() + ".\nKey: "
                                     + license.getLicenseKey(), juce::dontSendNotification);
        }
        else if (status == LicenseManager::Status::licensed)
        {
            titleLabel.setText ("Sevenpack Alpha License", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, AlphaPalette::get().sheetText());
            statusLabel.setText ("Licensed and active. Key: " + license.getLicenseKey(), juce::dontSendNotification);
        }
        else if (status == LicenseManager::Status::offlineGrace)
        {
            // v2.22.6 : avant, ce cas affichait les jours de DEMO restants.
            titleLabel.setText ("Sevenpack Alpha License", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, AlphaPalette::get().sheetText());
            statusLabel.setText ("Licensed (offline). Your key will be checked again\nnext time you are online.",
                                 juce::dontSendNotification);
        }
        else if (status == LicenseManager::Status::betaEnded)
        {
            // Cle beta dont la date de fin est passee (bloque meme hors ligne).
            titleLabel.setText ("Sevenpack Alpha beta ended", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0402b));
            const auto when = license.getBetaEndDateText();
            statusLabel.setText ((when.isNotEmpty() ? "Your beta license ended on " + when + "." : juce::String ("Your beta license has ended."))
                                     + " Thank you for testing!\nEnter a full license key to keep using Sevenpack Alpha.",
                                 juce::dontSendNotification);
        }
        else if (status == LicenseManager::Status::trial)
        {
            titleLabel.setText ("Sevenpack Alpha - Demo Mode", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, AlphaPalette::get().sheetText());
            statusLabel.setText ("Demo mode - " + juce::String (license.getTrialDaysLeft())
                                      + " day(s) left. Enter a key below to unlock the full version.",
                                  juce::dontSendNotification);
        }
        else if (status == LicenseManager::Status::invalid)
        {
            titleLabel.setText ("Activate Sevenpack Alpha", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0402b));
            auto err = license.getLastError();
            if (err == "network_error")
                statusLabel.setText ("Could not reach the license server.\nCheck your internet connection and try again.",
                                     juce::dontSendNotification);
            else
                statusLabel.setText (err.isNotEmpty() ? ("Invalid license: " + err) : "Invalid or revoked license.",
                                     juce::dontSendNotification);
        }
        else if (status == LicenseManager::Status::trialExpired)
        {
            titleLabel.setText ("Your 14-day trial has ended", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0402b));
            statusLabel.setText ("Enter a license key to keep using Sevenpack Alpha.", juce::dontSendNotification);
        }
        else
        {
            titleLabel.setText ("Activate Sevenpack Alpha", juce::dontSendNotification);
            statusLabel.setText ({}, juce::dontSendNotification);
        }

        showBetaJoinMessage();

        if (layoutChanged)
            resized();
    }

    LicenseManager& license;
    static constexpr juce::uint32 graceMs = 8000; // silent check window before an actual problem can show
    juce::uint32 launchedAtMs = 0;
    bool manuallyShown = false;
    bool betaMode = false; // vue "Join the beta program" (v2.23.3)
    juce::Rectangle<float> disclaimerArea;
    juce::Label titleLabel, statusLabel;
    juce::TextEditor keyEditor, firstNameEditor, lastNameEditor, emailEditor;
    juce::TextButton activateButton, demoButton, betaButton, closeButton, joinBetaButton, backButton;
    DiscordButton discordButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseOverlay)
};
