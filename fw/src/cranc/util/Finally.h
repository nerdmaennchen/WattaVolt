#pragma once

#include <utility>

namespace cranc {

template <typename FTor>
class Finally {
    FTor m_ftor;

public:
    template <typename FTor2>
    Finally(FTor2&& ftor) noexcept
        : m_ftor { std::forward<FTor>(ftor) } {};
    Finally(Finally&&) noexcept = default;
    Finally& operator=(Finally&&) noexcept = default;
    ~Finally()
    {
        m_ftor();
    }
};

template <typename FTor>
Finally(FTor &&) -> Finally<FTor>;

}