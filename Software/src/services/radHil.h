#pragma once

// Observe normal application work. These functions do not advance state,
// suppress updates, actuate hardware, or alter persistent settings.
void radHilStart();
void radHilProgress(bool idleAndReady);
