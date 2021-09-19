#include "MemManage.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cassert>

const char* FILE_PATH = "/dev/zero";

static MemoryManage manger;

void* kMalloc(std::size_t size)
{
	return static_cast<void*>(
		manger.allocate(static_cast<MemoryManage::length_type>(size))
	);
}

int  kFree(void* address)
{
	return static_cast<int>(
		manger.deallocate(static_cast<MemoryManage::address_type>(address))
	);
}

MemoryManage::MemoryManage(int maxFragmentationSize):
		maxFragmentationSize_(maxFragmentationSize),
		free_lists_(),
		memFd_(-1),
		requestMemSize_(0)
{
	memFd = open(FILE_PATH, O_RDWR);
	assert(-1 != memFd);

	auto[address, length] = _addMemory();
	Free_Node node(address, length);
	free_lists_.push_back(std::move(node));
}
MemoryManage::~MemoryManage()
{
	// ::munmap()	// TODO：后续考虑是否追加安全退出策略，即：munmap() 释放已映射的内存
	close(memFd);
}

void MemoryManage::errDealFunc(ERROR_TYPE error)
{
	switch (error)
	{
	case MemoryManage::ERROR_TYPE::OK:
		printf("!!!@ ... OK @!!!\n");
		break;
	case MemoryManage::ERROR_TYPE::NOT_ENOUGH_SPACE:
		printf("!!!@ NOT_ENOUGH_SPACE: 没有充足的内容可进行分配, 可考虑释放一些内存 @!!!\n");
		exit(EXIT_FAILURE);
		break;
	case MemoryManage::ERROR_TYPE::RELEASE_WRONG_ADDRESS:
		printf("!!!@ NOT_EXIST_TASK: 待释放的目标地址，并非 kMalloc 所 allocate 的内存 —— Segmentation Fault @!!!\n");
		exit(EXIT_FAILURE);
		break;
	default:
		printf("!!!@ unknown error @!!!\n");
		exit(EXIT_FAILURE);
		break;
	}
}

std::tuple<MemoryManage::address_type, MemoryManage::length_type>
MemoryManage::_addMemory()
{
	static length_type requestMemSize = 1024;

	// 写法 1 - （全平台通用，有点小麻烦）
	void* address = mmap(nullptr,  requestMemSize, 
						 PROT_READ | PROT_WRITE, MAP_SHARED, 
						 memFd, 0);

	// 写法 2 - （借助 Linux 特殊 mmap 参数）
	// void* address = mmap(nullptr, requestMemSize,
	// 						PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON,
	// 						0, 0);	// MAP_ANON -> Linux 特有参数，支持直接映射内存，不用再映射到 file 上 !

	if (MAP_FAILED == address)
		errDealFunc(ERROR_TYPE::NOT_ENOUGH_SPACE);

	auto temp = requestMemSize;
	requestMemSize_ += temp;
	requestMemSize *= 2;
	return std::make_tuple(static_cast<address_type>(address), temp);
}

void MemoryManage::addMemory()
{
	auto[temp_addr, temp_length] = _addMemory();

	auto itor = free_lists_.end();
	for (auto i = free_lists_.begin(); i != free_lists_.end(); ++i)
		if (temp_addr < i->address_)
		{
			itor = i;
			break;
		}

	combineAddressSegment(itor, std::move(Free_Node(temp_addr, temp_length)));
	// 这里可不可使用移动语义呢?	&& 移动语义我运用的真的是非常的不熟练
}

void MemoryManage::combineAddressSegment(
	std::list<struct Free_Node>::iterator itor,  struct Free_Node&& node)
{
	if (free_lists_.end() == itor)	// (begin, end]
	{
		// check previous node
		if (!free_lists_.empty())
		{
			auto&& tItor = std::prev(itor);
			address_type end_addr = tItor->address_ + tItor->length_;
			if (node.address_ == end_addr)
				tItor->length_ += node.length_;
		}
		else
			free_lists_.push_back(node);
	}
	else if (free_lists_.begin() == itor)	// [begin, end)
	{
		address_type end_addr = node.address_ + node.length_;
		if (end_addr == itor->address_)
		{
			itor->address_ = node.address_;
			itor->length_ += node.length_;
		}
		else
			free_lists_.insert(itor, node);
	}
	else	// (begin, end)
	{
		auto&& prevItor = std::prev(itor);
		address_type left_end_addr = prevItor->address_ + prevItor->length_;
		address_type right_begin_addr = itor->address_;
		address_type node_end_addr = node.address_ + node.length_;
		if (left_end_addr == node.address_ && node_end_addr == right_begin_addr)	// 3 node 同时重叠，directly combine
		{
			prevItor->length_ += + node.length_ + itor->length_;
			free_lists_.erase(itor);
		}
		else if (left_end_addr == node.address_ && node_end_addr != right_begin_addr)
		{
			prevItor->length_ += node.length_;
		}
		else if (left_end_addr != node.address_ && node_end_addr == right_begin_addr)
		{
			itor->length_ += node.length_;
		}
		else
		{
			free_lists_.insert(itor, node);
		}
	}
}

MemoryManage::address_type MemoryManage::allocate(length_type memorySize)
{
	// No.0 分配空间之前的预处理
	memorySize += sizeof(MemoryManage::MemManage_Node);

	// No.1 确定是否有足够的空间用于存储 —— 采用 “首次匹配” 内存分配算法	
	auto itor = free_lists_.end();
	Reburn:		// 打破禁忌，违规使用了 goto Grammar
	for (auto it = free_lists_.begin() ; it != free_lists_.end(); ++it)
		if ((*it).length_ >= memorySize)
		{
			itor = it;
			break;
		}
	if (free_lists_.end() == itor)
	{
		addMemory();
		goto Reburn;
	}

	// No.2 判断是否会产生最低阈值的 Fragmentation(内存碎片) —— 我们设定了可以产生的内存碎片的 MAX-value
	address_type addr  = 0x000;
	auto& temp = *itor;

	addr = temp.address_;
	if (maxFragmentationSize_ >= temp.length_ - memorySize)
	{
		memorySize = temp.length_;			// 为了避免过小的内存碎片，所以将在会产生内存碎片的情况的 temp 的所有内存都分给了 node
		free_lists_.erase(itor);
	}
	else
	{
		temp.address_ += memorySize;
		temp.length_ -= memorySize;
	}

	// No.3 嵌入 MemManageNode
	MemManage_Node* memNode = reinterpret_cast<MemManage_Node*>(addr);	// char* -> MemMange_Node*
	memNode->size_ = memorySize;
	memNode->magic_ = MemManage_Node::MAGIC;
	return addr + sizeof(MemManage_Node);
}

MemoryManage::ERROR_TYPE MemoryManage::deallocate(address_type address)
{
	// No.0 将内存地址偏移到真是目标地址上 —— 即：定位到嵌入的 MemManageNode 的地址上 
	address -= sizeof(MemManage_Node);
	
	// No.1 校验，检查待 deallocate 的 address 是否为 allocate 申请的内存
	length_type length = 0;
	MemManage_Node* ptr = reinterpret_cast<MemManage_Node*>(address);
	if (ptr->MAGIC != ptr->magic_)
		errDealFunc(ERROR_TYPE::RELEASE_WRONG_ADDRESS);

	// No.1.5 继续完成重定向的转化
	length = ptr->size_;

	// No.2 搜寻 address 在 free_lists_ 中的位置
	auto targetItor = free_lists_.end();
	for (auto i = free_lists_.begin(); i != free_lists_.end(); ++i)
		if (address < i->address_)
		{
			targetItor = i;
			break;
		}

	// No.3 执行合并动作
	combineAddressSegment(targetItor, Free_Node(address, length));

	return ERROR_TYPE::OK;
}
