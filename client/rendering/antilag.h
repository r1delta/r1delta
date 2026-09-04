#pragma once

struct ID3D11Device;

void RegisterAntiLagConVars();
void AntiLagOnDeviceReady(ID3D11Device* device);
void AntiLagBeforeInputPoll();
void AntiLagOnDeviceShutdown();
