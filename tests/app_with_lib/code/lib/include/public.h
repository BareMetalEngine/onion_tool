// Public header for the project - stuff here is automaticaly visible to ALL projects that declare this project as dependency

#pragma once

#include <memory>

class FooClass;
typedef std::unique_ptr<FooClass> FooClassPtr;