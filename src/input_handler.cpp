// ============================================================
// input_handler.cpp – Static member definitions
// ============================================================

#include "input_handler.h"

// Encoder state
uint8_t InputHandler::lastEncoderState_ = 0;
int8_t InputHandler::encoderAccum_ = 0;

// Button state
uint32_t InputHandler::buttonPressTime_ = 0;
bool InputHandler::buttonPressed_ = false;
bool InputHandler::longPressFired_ = false;
bool InputHandler::lastRawState_ = false;
uint32_t InputHandler::lastStateChangeTime_ = 0;
