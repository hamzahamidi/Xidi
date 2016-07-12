/*****************************************************************************
 * XinputControllerDirectInput
 *      Hook and helper for older DirectInput games.
 *      Fixes issues associated with certain Xinput-based controllers.
 *****************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2016
 *****************************************************************************
 * ApiDirectInput8.h
 *      Pulls in all DirectInput GUID definitions to avoid linking with
 *      the "dinput8.lib" library.
 *****************************************************************************/

#define INITGUID
#include "ApiDirectInput8.h"