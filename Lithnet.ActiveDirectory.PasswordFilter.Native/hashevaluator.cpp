#include "stdafx.h"
#include "hasher.h"
#include <fstream>
#include "registry.h"
#include "hashevaluator.h"
#include "shlwapi.h"
#include <sstream>
#include "hasher.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include "esestore.h"

const int STORE_VERSION = 1;


template<typename T>
std::wstring ToHexString(T first, T last, bool use_uppercase = true, bool insert_spaces = false)
{
	std::wstringstream ss;
	ss << std::hex << std::setfill(L'0');

	if (use_uppercase)
	{
		ss << std::uppercase;
	}

	while (first != last)
	{
		ss << std::setw(2) << static_cast<int>(*first++);

		if (insert_spaces && first != last)
		{
			ss << " ";
		}
	}

	return ss.str();
}

bool IsPasswordInStore(std::wstring password)
{
	BYTE *hash = NULL;

	try
	{
		hash = new BYTE[20];

		GetSha1HashBytes(password, hash, 20);

		bool result;

		int hashCheckMode = GetRegValue(L"HashCheckMode", 0);

		if (hashCheckMode == 0)
		{
			OutputDebugString(L"IsHashInEseStore");
			result = IsHashInEseStore(hash);
		}
		else if (hashCheckMode == 1)
		{
			OutputDebugString(L"IsHashInStorev1");
			result = IsHashInStorev1(hash);
		}
		else if (hashCheckMode == 2)
		{
			OutputDebugString(L"IsHashInStore");
			result = IsHashInStore(password);
		}
		else if (hashCheckMode == 3)
		{
			OutputDebugString(L"IsHashInStorev2");
			result = IsHashInStorev2(hash);
		}

		if (hash)
		{
			delete[] hash;
		}

		return result;
	}
	catch (...)
	{
		if (hash)
		{
			delete[] hash;
		}

		throw;
	}
}

bool IsHashInEseStore(BYTE* hash)
{
	return esestore::getInstance().IsHashInDb(hash);
}

bool IsHashInStore(std::wstring hash)
{
	std::wstring range = hash.substr(0, 5);
	return IsHashInStore(hash, range);
}

bool IsHashInStorev1(BYTE* hash)
{
	std::wstring range = ToHexString(hash, hash + 3).substr(0, 5);
	return IsHashInStorev1(hash, range);
}

bool IsHashInStorev2(BYTE* hash)
{
	std::wstring range = ToHexString(hash, hash + 2);
	return IsHashInStorev2(hash, range);
}

bool IsHashInStore(std::wstring hash, std::wstring range)
{
	std::wstring path = GetStoreFileName(range);

	DWORD attr = GetFileAttributes(path.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		return false;
	}

	return IsHashInTextFileBinarySearch(path, hash);
}

bool IsHashInStorev1(BYTE* hash, std::wstring range)
{
	std::wstring path = GetStoreFileNamev1(range);

	DWORD attr = GetFileAttributes(path.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		return false;
	}

	return IsHashInBinaryFilev1(path, hash);
}


bool IsHashInStorev2(BYTE* hash, std::wstring range)
{
	std::wstring path = GetStoreFileNamev2(range);

	DWORD attr = GetFileAttributes(path.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		return false;
	}

	return IsHashInBinaryFilev2(path, hash);
}


std::wstring GetStoreFileName(std::wstring range)
{
	std::wstring path = GetRegValue(L"Store", L"");

	if (path == L"")
	{
		throw new std::invalid_argument("No store path was specified");
	}

	if (!path.empty() && *path.rbegin() != '\\')
		path += '\\';

	path += L"SHA1\\";
	path += range;
	path += L".txt";

	return path;
}

std::wstring GetStoreFileNamev1(std::wstring range)
{
	std::wstring path = GetRegValue(L"Store", L"");

	if (path == L"")
	{
		throw new std::invalid_argument("No store path was specified");
	}

	if (!path.empty() && *path.rbegin() != '\\')
		path += '\\';

	path += L"v1\\";
	path += range;
	path += L".db";

	return path;
}

std::wstring GetStoreFileNamev2(std::wstring range)
{
	std::wstring path = GetRegValue(L"Store", L"");

	if (path == L"")
	{
		throw new std::invalid_argument("No store path was specified");
	}

	if (!path.empty() && *path.rbegin() != '\\')
		path += '\\';

	path += L"v2\\";
	path += range;
	path += L".db";

	return path;
}

bool IsHashInFileTextFile(std::wstring filename, std::wstring hash)
{
	return false;
	std::wifstream file(filename.c_str());
	std::wstring line;

	while (std::getline(file, line))
	{
		if (line.compare(hash) == 0)
		{
			return true;
		}
	}

	return false;
}

bool hasBOM(std::ifstream & is)
{
	/* Read the first byte. */
	char const c0 = is.get();
	if (c0 != '\xEF') {
		is.putback(c0);
		return false;
	}

	/* Read the second byte. */
	char const c1 = is.get();
	if (c1 != '\xBB') {
		is.putback(c1);
		is.putback(c0);
		return false;
	}

	/* Peek the third byte. */
	char const c2 = is.peek();
	if (c2 != '\xBF') {
		is.putback(c1);
		is.putback(c0);
		return false;
	}

	return true; // This file contains a BOM for UTF-8.
}

bool IsHashInTextFileBinarySearch(std::wstring filename, std::wstring hash)
{
	char hashVal[SHA1_HASH_LENGTH + 1];

	wcstombs(hashVal, hash.c_str(), SHA1_HASH_LENGTH + 1);

	std::ifstream file(filename.c_str(), std::ios::binary | std::ios::in);

	int headerCount = 0;
	if (hasBOM(file))
	{
		headerCount = 3;
	}

	std::string search;
	int firstRow = 0, currentRow = 0, lastRow = 0, length = 0;
	bool found = false;

	file.seekg(0, std::ios::end);
	length = file.tellg();

	file.seekg(0, std::ios::beg);

	lastRow = ((length - headerCount) / (SHA1_HASH_LENGTH + SHA1_HASH_ROW_TERMINATOR_LENGTH));

	while (firstRow <= lastRow)
	{
		currentRow = (firstRow + lastRow + 1) / 2;
		file.seekg(headerCount + (currentRow * (SHA1_HASH_LENGTH + SHA1_HASH_ROW_TERMINATOR_LENGTH)), std::ios::beg);

		char rowData[SHA1_HASH_LENGTH];

		file.read(rowData, SHA1_HASH_LENGTH);

		search = std::string(rowData, SHA1_HASH_LENGTH);

		if (search < hashVal)
		{
			firstRow = currentRow + 1;
		}
		else if (search > hashVal)
		{
			lastRow = currentRow - 1;
		}
		else
		{
			return true;
		}
	}

	return false;
}

bool IsHashInBinaryFilev1(std::wstring filename, BYTE* hashBytes)
{
	std::ifstream file(filename.c_str(), std::ios::binary | std::ios::in);

	int firstRow = 0, currentRow = 0, lastRow = 0, length = 0;
	bool found = false;
	OutputDebugString(L"Searching");
	OutputDebugString(filename.c_str());
	file.seekg(0, std::ios::end);
	length = file.tellg();

	file.seekg(0, std::ios::beg);

	if (length % SHA1_BINARY_HASH_LENGTH != 0)
	{
		throw new std::invalid_argument("The hash store is corrupted");
	}

	lastRow = ((length) / (SHA1_BINARY_HASH_LENGTH));
	int loops = 0;

	while (firstRow <= lastRow)
	{
		loops++;
		currentRow = (firstRow + lastRow + 1) / 2;
		file.seekg((currentRow * (SHA1_BINARY_HASH_LENGTH)), std::ios::beg);

		char rowData[SHA1_BINARY_HASH_LENGTH];

		file.read(rowData, SHA1_BINARY_HASH_LENGTH);

		int result = memcmp(rowData, hashBytes, SHA1_BINARY_HASH_LENGTH);

		if (result < 0)
		{
			firstRow = currentRow + 1;
		}
		else if (result > 0)
		{
			lastRow = currentRow - 1;
		}
		else
		{
			return true;
		}
	}

	OutputDebugString(std::to_wstring(loops).c_str());

	return false;
}

bool IsHashInBinaryFilev2(std::wstring filename, BYTE* hashBytes)
{
	std::ifstream file(filename.c_str(), std::ios::binary | std::ios::in);

	int firstRow = 0, currentRow = 0, lastRow = 0, length = 0;
	bool found = false;
	OutputDebugString(L"Searching");
	OutputDebugString(filename.c_str());
	file.seekg(0, std::ios::end);
	length = file.tellg();

	file.seekg(0, std::ios::beg);

	BYTE* partialHashBytes = hashBytes + 2;

	if (length % SHA1_PARTIAL_BINARY_HASH_LENGTH != 0)
	{
		throw new std::invalid_argument("The hash store is corrupted");
	}

	lastRow = ((length) / (SHA1_PARTIAL_BINARY_HASH_LENGTH));
	int loops = 0;

	while (firstRow <= lastRow)
	{
		loops++;
		currentRow = (firstRow + lastRow + 1) / 2;
		file.seekg((currentRow * (SHA1_PARTIAL_BINARY_HASH_LENGTH)), std::ios::beg);

		char rowData[SHA1_PARTIAL_BINARY_HASH_LENGTH];

		file.read(rowData, SHA1_PARTIAL_BINARY_HASH_LENGTH);

		int result = memcmp(rowData, partialHashBytes, SHA1_PARTIAL_BINARY_HASH_LENGTH);

		if (result < 0)
		{
			firstRow = currentRow + 1;
		}
		else if (result > 0)
		{
			lastRow = currentRow - 1;
		}
		else
		{
			return true;
		}
	}

	OutputDebugString(std::to_wstring(loops).c_str());

	return false;
}