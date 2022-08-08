#include "build.h"
#include "lib/include/foo.h"

#include <iostream>

#if 0
namespace onion
{
	enum class TypeKind
	{
		Simple,
		Compound,
		Templated,
		Enumeration,
	};

	struct TypePropertyInfo
	{
		uint32_t offset = 0;
		const TypeInfo* type = nullptr;
		const char* name = nullptr;
		const char* hint = "";
		const char* params = "";
	};

	struct TypeOption
	{
		uint64_t value = 0;
		const char* name = nullptr;
		const char* hint = "";
	};

	struct TypeInfo
	{
		TypeKind kind = TypeKind::Simple;

		bool flagAbstract = false;
		TypeInfo* parentType = nullptr;

		uint16_t alignment = 0;
		uint32_t size = 0;
		uint64_t hash = 0;
		const char* hint = "";
		const char* name = nullptr;

		const TypePropertyInfo** propertyTable = nullptr;
		uint16_t propertyTableSize = 0;

		const TypeOption** optionTable = nullptr;
		uint16_t optionTableSize = 0;

		const TypeInfo** templateTable = nullptr;
		uint16_t templateTableSize = 0;
	};

	struct TypeList
	{
		const char* projectName = nullptr;

		const TypeInfo** typeTable = nullptr;
		uint32_t typeTableSize = 0;
	};
}
#endif

/*
namespace bt
{
	template< typename T >
	Type GetTypeObject()
	{
		static const typeObject = RTTI::BuildTypeObject(onion::GetTypeInfo<T>());
		return typeObject;
	}
}
*/

int main()
{
	auto foo = MakeFoo();
	auto value = foo->test();
	std::cout << "The foo is: " << value << "\n"; 
	return 0;
}