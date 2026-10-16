#pragma once

#include <JuceHeader.h>
#include "LicenseManager.h"
#include "AlphaSkin.h"

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
        keyEditor.setTextToShowWhenEmpty ("License key (required)", juce::Colours::grey);
        keyEditor.setJustification (juce::Justification::centred);
        keyEditor.onReturnKey = [this] { activate(); };

        addAndMakeVisible (firstNameEditor);
        firstNameEditor.setTextToShowWhenEmpty ("First name (optional)", juce::Colours::grey);
        firstNameEditor.setJustification (juce::Justification::centred);
        firstNameEditor.onReturnKey = [this] { activate(); };

        addAndMakeVisible (lastNameEditor);
        lastNameEditor.setTextToShowWhenEmpty ("Last name (optional)", juce::Colours::grey);
        lastNameEditor.setJustification (juce::Justification::centred);
        lastNameEditor.onReturnKey = [this] { activate(); };

        addAndMakeVisible (emailEditor);
        emailEditor.setTextToShowWhenEmpty ("Email (required)", juce::Colours::grey);
        emailEditor.setJustification (juce::Justification::centred);
        emailEditor.onReturnKey = [this] { activate(); };

        addAndMakeVisible (activateButton);
        activateButton.setButtonText ("Activate");
        activateButton.onClick = [this] { activate(); };

        addAndMakeVisible (demoButton);
        demoButton.setButtonText ("Start 14-Day Demo");
        demoButton.onClick = [this] { license.startDemo(); };

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
        AlphaDraw::overlaySheet (g, getLocalBounds().withSizeKeepingCentre (380, 400).toFloat(), 12.0f);
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
        auto area = getLocalBounds().withSizeKeepingCentre (320, 320);
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
        area.removeFromTop (10);
        statusLabel.setBounds (area.removeFromTop (40));

        if (closeButton.isVisible())
        {
            area.removeFromTop (6);
            closeButton.setBounds (area.removeFromTop (28).withSizeKeepingCentre (120, 28));
        }
    }

private:
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
        statusLabel.setColour (juce::Label::textColourId, AlphaPalette::get().sheetText());
        statusLabel.setText ("Checking...", juce::dontSendNotification);
        license.activate (key, firstNameEditor.getText(), lastNameEditor.getText(), email);
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
                          || status == LicenseManager::Status::trialExpired;

        // Grace window: the check runs silently in the background on every
        // launch -- never slam the overlay up the instant the editor opens.
        // Only surface it if the problem is still there after a few seconds,
        // so a normal (licensed/trial) launch never sees it flash at all.
        bool graceElapsed = (juce::Time::getMillisecondCounter() - launchedAtMs) >= graceMs;

        setVisible (manuallyShown || (isProblem && graceElapsed));
        activateButton.setEnabled (status != LicenseManager::Status::activating);

        bool shouldShowDemoButton = license.canStartDemo();
        bool layoutChanged = (shouldShowDemoButton != demoButton.isVisible())
                              || (closeButton.isVisible() != canClose);

        demoButton.setVisible (shouldShowDemoButton);
        closeButton.setVisible (canClose);

        if (status == LicenseManager::Status::licensed)
        {
            titleLabel.setText ("Sevenpack Alpha License", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, AlphaPalette::get().sheetText());
            statusLabel.setText ("Licensed and active. Key: " + license.getLicenseKey(), juce::dontSendNotification);
        }
        else if (status == LicenseManager::Status::trial || status == LicenseManager::Status::offlineGrace)
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

        if (layoutChanged)
            resized();
    }

    LicenseManager& license;
    static constexpr juce::uint32 graceMs = 8000; // silent check window before an actual problem can show
    juce::uint32 launchedAtMs = 0;
    bool manuallyShown = false;
    juce::Label titleLabel, statusLabel;
    juce::TextEditor keyEditor, firstNameEditor, lastNameEditor, emailEditor;
    juce::TextButton activateButton, demoButton, closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseOverlay)
};
