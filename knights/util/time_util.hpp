#pragma once

class time_util {
public:
    static uint32_t origin;
    static uint32_t hour;
    static uint32_t min;

public:
    static uint32_t getnow() {
        return publication_time() / 1'000'000 - origin;
    }
};

uint32_t time_util::origin = 1500000000;
uint32_t time_util::hour = 3600;
uint32_t time_util::min = 60;
