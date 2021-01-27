#pragma once
#include <iostream>
#include <sstream>

namespace TS
{
template <typename TStream, typename T> inline
auto &&FormatVal(TStream &&out, T &&val)
{
	out << std::forward<T>(val);
	return (std::forward<TStream>(out));
}

template <typename TStream> inline
auto &&_FormatVals(TStream &&out)
{
	return (std::forward<TStream>(out));
}

template <typename TStream, typename T, typename... TT> inline
auto &&_FormatVals(TStream &&out, T &&val, TT&&... args)
{
	FormatVal(out, ", ");
	FormatVal(out, std::forward<T>(val));
	return (_FormatVals(std::forward<TStream>(out), std::forward<TT>(args)...));
}

template <typename TStream, typename T, typename... TT> inline
auto &&FormatVals(TStream &&out, T &&val, TT&&... args)
{
	FormatVal(out, std::forward<T>(val));
	return (_FormatVals(std::forward<TStream>(out), std::forward<TT>(args)...));
}

template <typename... TT> inline
auto FormatStr(TT&&... args)
{
	return FormatVals(std::stringstream(), std::forward<TT>(args)...);
}

}
