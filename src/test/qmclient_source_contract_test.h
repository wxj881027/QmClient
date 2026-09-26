#ifndef TEST_QMCLIENT_SOURCE_CONTRACT_TEST_H
#define TEST_QMCLIENT_SOURCE_CONTRACT_TEST_H

#include <test/test.h>

#include <algorithm>
#include <initializer_list>
#include <string>

namespace
{
	[[maybe_unused]] std::string ReadRepoFile(const char *pPath)
	{
		return ReadTestSourceFile(pPath);
	}

	[[maybe_unused]] std::string ReadTextFile(const char *pPath)
	{
		std::string Source = ReadTestSourceFile(pPath);
		Source.erase(std::remove(Source.begin(), Source.end(), '\r'), Source.end());
		return Source;
	}

	[[maybe_unused]] bool ContainsAll(const std::string &Source, std::initializer_list<const char *> Needles)
	{
		for(const char *pNeedle : Needles)
		{
			if(Source.find(pNeedle) == std::string::npos)
				return false;
		}
		return true;
	}

	[[maybe_unused]] bool ContainsAny(const std::string &Source, std::initializer_list<const char *> Needles)
	{
		for(const char *pNeedle : Needles)
		{
			if(Source.find(pNeedle) != std::string::npos)
				return true;
		}
		return false;
	}

	[[maybe_unused]] std::string ExtractSourceFunctionBody(const std::string &Source, const char *pSignature)
	{
		const size_t SignaturePos = Source.find(pSignature);
		if(SignaturePos == std::string::npos)
			return {};

		const size_t BodyStart = Source.find('{', SignaturePos);
		if(BodyStart == std::string::npos)
			return {};

		int Depth = 0;
		for(size_t i = BodyStart; i < Source.size(); ++i)
		{
			if(Source[i] == '{')
				++Depth;
			else if(Source[i] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, i - BodyStart + 1);
			}
		}
		return {};
	}

	// 兼容旧合同测试的命名，统一复用同一份括号匹配实现。
	[[maybe_unused]] std::string SourceFunctionBody(const std::string &Source, const std::string &Signature)
	{
		return ExtractSourceFunctionBody(Source, Signature.c_str());
	}

	[[maybe_unused]] std::string FunctionBody(const std::string &Source, const std::string &Signature)
	{
		return ExtractSourceFunctionBody(Source, Signature.c_str());
	}

	[[maybe_unused]] std::string ExtractSourceBlock(const std::string &Source, const char *pBeginMarker, const char *pEndMarker)
	{
		const size_t Begin = Source.find(pBeginMarker);
		if(Begin == std::string::npos)
			return {};
		const size_t End = Source.find(pEndMarker, Begin);
		if(End == std::string::npos)
			return Source.substr(Begin);
		return Source.substr(Begin, End - Begin);
	}

	[[maybe_unused]] std::string BlockBodyAfter(const std::string &Source, const std::string &Anchor)
	{
		const size_t AnchorPos = Source.find(Anchor);
		if(AnchorPos == std::string::npos)
			return {};
		const size_t BodyStart = Source.find('{', AnchorPos);
		if(BodyStart == std::string::npos)
			return {};
		int Depth = 0;
		for(size_t i = BodyStart; i < Source.size(); ++i)
		{
			if(Source[i] == '{')
				++Depth;
			else if(Source[i] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, i - BodyStart);
			}
		}
		return {};
	}

	[[maybe_unused]] size_t CountSubstring(const std::string &Haystack, const std::string &Needle)
	{
		if(Needle.empty())
			return 0;

		size_t Count = 0;
		size_t Pos = 0;
		while((Pos = Haystack.find(Needle, Pos)) != std::string::npos)
		{
			++Count;
			Pos += Needle.size();
		}
		return Count;
	}
}

#endif
