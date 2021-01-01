/*****************************************************************************
 * Xidi
 *   DirectInput interface for XInput controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016-2020
 *************************************************************************//**
 * @file VirtualController.cpp
 *   Implementation of a complete virtual controller.
 *****************************************************************************/

#include "ApiWindows.h"
#include "ControllerMapper.h"
#include "ControllerTypes.h"
#include "Message.h"
#include "VirtualController.h"
#include "XInputInterface.h"

#include <cstdint>
#include <memory>
#include <mutex>


namespace Xidi
{
    // -------- INTERNAL FUNCTIONS ----------------------------------------- //

    /// Maps a value in one range to its corresponding value in another range.
    /// Ranges are specified as origin values and displacements, essentially one-dimensional vectors with direction either positive (maximum displacement value is greater than origin value) or negative (maximum displacement value is less than origin value).
    /// It is not necessary that the direction of the vectors be the same for both old and new range.
    /// @param [in] oldRangeValue Raw value to transform in the old range.
    /// @param [in] oldRangeOrigin Origin value of the old range.
    /// @param [in] oldRangeDispMax Maximum displacement value in the old range.
    /// @param [in] newRangeOrigin Origin value of the new range.
    /// @param [in] newRangeDispMax Maximum displacement value in the new range.
    /// @return Result of mapping the input value from the old range to the new range.
    static inline int32_t MapValueInRangeToRange(int32_t oldRangeValue, int32_t oldRangeOrigin, int32_t oldRangeDispMax, int32_t newRangeOrigin, int32_t newRangeDispMax)
    {
        return newRangeOrigin + (((oldRangeValue - oldRangeOrigin) * (newRangeDispMax - newRangeOrigin)) / (oldRangeDispMax - oldRangeOrigin));
    }

    /// Transforms a raw axis value using the supplied axis properties.
    /// @param [in] axisValueRaw Raw axis value as obtained from a mapper.
    /// @param [in] axisProperties Axis properties to apply.
    /// @return Axis value that results from applying the transformation.
    static int32_t TransformAxisValue(int32_t axisValueRaw, const VirtualController::SAxisProperties& axisProperties)
    {
        if (axisValueRaw > Controller::kAnalogValueNeutral)
        {
            if (axisValueRaw <= axisProperties.deadzoneRawCutoffPositive)
                return axisProperties.rangeNeutral;
            else if (axisValueRaw >= axisProperties.saturationRawCutoffPositive)
                return axisProperties.rangeMax;
            else
                return MapValueInRangeToRange(axisValueRaw, axisProperties.deadzoneRawCutoffPositive, axisProperties.saturationRawCutoffPositive, axisProperties.rangeNeutral, axisProperties.rangeMax);
        }
        else
        {
            if (axisValueRaw >= axisProperties.deadzoneRawCutoffNegative)
                return axisProperties.rangeNeutral;
            else if (axisValueRaw <= axisProperties.saturationRawCutoffNegative)
                return axisProperties.rangeMin;
            else
                return MapValueInRangeToRange(axisValueRaw, axisProperties.deadzoneRawCutoffNegative, axisProperties.saturationRawCutoffNegative, axisProperties.rangeNeutral, axisProperties.rangeMin);
        }
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "VirtualController.h" for documentation.

    void VirtualController::ApplyProperties(Controller::SState* controllerState) const
    {
        const Controller::SCapabilities& controllerCapabilities = mapper.GetCapabilities();

        for (int i = 0; i < controllerCapabilities.numAxes; ++i)
        {
            const Controller::EAxis axis = controllerCapabilities.axisType[i];
            controllerState->axis[(int)axis] = TransformAxisValue(controllerState->axis[(int)axis], properties.axis[(int)axis]);
        }
    }

    // --------

    void VirtualController::GetState(Controller::SState* controllerState)
    {
        std::scoped_lock lock(controllerMutex);

        if (true == stateRefreshNeeded)
            RefreshState();

        memcpy(controllerState, &state, sizeof(state));
        stateRefreshNeeded = true;
    }

    // --------

    bool VirtualController::RefreshState(void)
    {
        XINPUT_STATE xinputState;
        SStateIdentifier newStateIdentifier = {.packetNumber = 0, .errorCode = xinput->GetState(kControllerIdentifier, &xinputState)};

        std::scoped_lock lock(controllerMutex);
        stateRefreshNeeded = false;

        // Most of the logic in this block is for debugging by outputting messages. The actual functionality is very simple.
        // On success, the packet number is updated to the value received from XInput, otherwise it is left at 0.
        // On failure, the XInput state is zeroed out so that the controller appears to be in a completely neutral state.
        switch (newStateIdentifier.errorCode)
        {
        case ERROR_SUCCESS:
            newStateIdentifier.packetNumber = xinputState.dwPacketNumber;
            switch (stateIdentifier.errorCode)
            {
            case ERROR_SUCCESS:
                break;

            case ERROR_DEVICE_NOT_CONNECTED:
                Message::OutputFormatted(Message::ESeverity::Info, L"Virtual controller %u: connected.", kControllerIdentifier);
                break;

            default:
                Message::OutputFormatted(Message::ESeverity::Warning, L"Virtual controller %u: previous error condition with code 0x%08x is now cleared.", kControllerIdentifier, stateIdentifier.errorCode);
                break;
            }
            break;

        case ERROR_DEVICE_NOT_CONNECTED:
            ZeroMemory(&xinputState, sizeof(xinputState));
            if (newStateIdentifier.errorCode != stateIdentifier.errorCode)
                Message::OutputFormatted(Message::ESeverity::Info, L"Virtual controller %u: disconnected.", kControllerIdentifier);
            break;

        default:
            ZeroMemory(&xinputState, sizeof(xinputState));
            if (newStateIdentifier.errorCode != stateIdentifier.errorCode)
                Message::OutputFormatted(Message::ESeverity::Warning, L"Virtual controller %u: encountered error code 0x%08x during state refresh.", kControllerIdentifier, newStateIdentifier.errorCode);
            break;
        }

        // If the state identifier is effectively the same then there is nothing further to do.
        // Different packet numbers always mean different states.
        // However, a change in error code without without a transition to or from ERROR_SUCCESS does not indicate a change in state.
        if ((newStateIdentifier.packetNumber == stateIdentifier.packetNumber) || ((ERROR_SUCCESS == newStateIdentifier.errorCode) && (ERROR_SUCCESS != stateIdentifier.errorCode)) || ((ERROR_SUCCESS != newStateIdentifier.errorCode) && (ERROR_SUCCESS == stateIdentifier.errorCode)))
            return false;
        stateIdentifier = newStateIdentifier;
        
        Controller::SState newState;
        mapper.MapXInputState(&newState, xinputState.Gamepad);
        ApplyProperties(&newState);

        // Based on the mapper and the applied properties, a change in XInput controller state might not necessarily mean a change in virtual controller state.
        // For example, deadzone might result in filtering out changes in analog stick position, or if a particular XInput controller element is ignored by the mapper then a change in that element does not influence the virtual controller state.
        if (newState == state)
            return false;
        state = newState;

        return true;
    }

    // --------

    bool VirtualController::SetAxisDeadzone(Controller::EAxis axis, uint32_t deadzone)
    {
        if ((deadzone >= kAxisDeadzoneMin) && (deadzone <= kAxisDeadzoneMax))
        {
            std::scoped_lock lock(controllerMutex);
            properties.axis[(int)axis].SetDeadzone(deadzone);
            return true;
        }

        return false;
    }

    // --------

    bool VirtualController::SetAxisRange(Controller::EAxis axis, int32_t rangeMin, int32_t rangeMax)
    {
        if (rangeMax > rangeMin)
        {
            std::scoped_lock lock(controllerMutex);
            properties.axis[(int)axis].SetRange(rangeMin, rangeMax);
            return true;
        }

        return false;
    }

    // --------

    bool VirtualController::SetAxisSaturation(Controller::EAxis axis, uint32_t saturation)
    {
        if ((saturation >= kAxisSaturationMin) && (saturation <= kAxisSaturationMax))
        {
            std::scoped_lock lock(controllerMutex);
            properties.axis[(int)axis].SetSaturation(saturation);
            return true;
        }

        return false;
    }

    // --------

    bool VirtualController::SetAllAxisDeadzone(uint32_t deadzone)
    {
        if ((deadzone >= kAxisDeadzoneMin) && (deadzone <= kAxisDeadzoneMax))
        {
            std::scoped_lock lock(controllerMutex);
            for (int i = 0; i < _countof(properties.axis); ++i)
                properties.axis[(int)i].SetDeadzone(deadzone);
            return true;
        }

        return false;
    }

    // --------

    bool VirtualController::SetAllAxisRange(int32_t rangeMin, int32_t rangeMax)
    {
        if (rangeMax > rangeMin)
        {
            std::scoped_lock lock(controllerMutex);
            for (int i = 0; i < _countof(properties.axis); ++i)
                properties.axis[(int)i].SetRange(rangeMin, rangeMax);
            return true;
        }

        return false;
    }

    // --------

    bool VirtualController::SetAllAxisSaturation(uint32_t saturation)
    {
        if ((saturation >= kAxisSaturationMin) && (saturation <= kAxisSaturationMax))
        {
            std::scoped_lock lock(controllerMutex);
            for (int i = 0; i < _countof(properties.axis); ++i)
                properties.axis[(int)i].SetSaturation(saturation);
            return true;
        }

        return false;
    }
}