#pragma once

// Generated by xmake from config/Project.hpp.in

#include <semver.hpp>

namespace App::Project
{
constexpr auto Name = "Codeware";
constexpr auto Author = "psiberx";

constexpr auto NameW = L"Codeware";
constexpr auto AuthorW = L"psiberx";

constexpr auto Version = semver::from_string_noexcept("1.0.0").value();
}