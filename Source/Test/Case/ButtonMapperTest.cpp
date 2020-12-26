/*****************************************************************************
 * Xidi
 *   DirectInput interface for XInput controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016-2020
 *************************************************************************//**
 * @file ButtonMapperTest.cpp
 *   Unit tests for controller element mappers that contribute to a virtual
 *   button.
 *****************************************************************************/

#include "ApiWindows.h"
#include "ControllerElementMapper.h"
#include "ControllerTypes.h"
#include "TestCase.h"

#include <cstdint>


namespace XidiTest
{
    using namespace ::Xidi::Controller;


    // -------- TEST CASES ------------------------------------------------- //

    // Creates one button mapper for each possible virtual button and verifies that each correctly identifies its target virtual controller element.
    TEST_CASE(ButtonMapper_GetTargetElement)
    {
        for (int i = 0; i < (int)EButton::Count; ++i)
        {
            const ButtonMapper mapper((EButton)i);
            TEST_ASSERT(EElementType::Button == mapper.GetTargetElementType());
            TEST_ASSERT(i == mapper.GetTargetElementIndex());
        }
    }
}
