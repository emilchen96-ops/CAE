#pragma once

#include "SphJobDefinition.hpp"

class SphJobWriter {
public:
    SphJobWriteResult write(const SphJobInput& input) const;
    SphJobWriteResult write(const SphMultiBodyJobInput& input) const;
};
