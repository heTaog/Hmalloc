#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#include <cstdint>

#include <list>
#include <tuple>

#pragma pack(4)	// 调整为 4 字节对齐

class MemoryManage {
public:
	enum class ERROR_TYPE : int {
		OK = 0,									// 正常返回
		NOT_ENOUGH_SPACE = -1,					// 内存空间不足
		RELEASE_WRONG_ADDRESS = -2				// 释放了错误的地址 —— 当前地址，不属于 MemoryMange allocated
	};

	typedef char* address_type;
	typedef int64_t length_type;

private:
	struct Free_Node 
	{
		Free_Node(): address_(nullptr), length_(0) {}
		Free_Node(address_type addr, length_type leng) : 
			address_(addr), length_(leng) {}

		address_type address_;		// 空闲内存的起始地址
		length_type length_;		// 空闲内存的剩余内存长度
	};
	struct MemManage_Node
	{
		length_type size_;
		int magic_;
		static constexpr int MAGIC = 1234567;
	};

public:
	MemoryManage(int maxFragmentationSize = 3);
	~MemoryManage();

	MemoryManage::address_type allocate(length_type memorySize);
	ERROR_TYPE deallocate(address_type address);

private:
	void addMemory();
	std::tuple<address_type, length_type> _addMemory();
	void errDealFunc(ERROR_TYPE error);
	void combineAddressSegment(std::list<struct Free_Node>::iterator itor, struct Free_Node&& node);

	friend void* kMalloc(std::size_t size);
	friend int 	 kFree(void* address);

private:
	const int maxFragmentationSize_;
	std::list<struct Free_Node> free_lists_;

	int memFd_;
	size_t requestMemSize_;
};

void* kMalloc(std::size_t size);
int   kFree(void* address);

#endif	// MemoryManager.h