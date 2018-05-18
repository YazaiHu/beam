#pragma once

#include "common.h"

namespace beam
{

class RadixTree
{
protected:

	struct Node
	{
		uint16_t m_Bits;
		static const uint16_t s_Clean = 1 << 0xf;
		static const uint16_t s_Leaf = 1 << 0xe;

		uint16_t get_Bits() const;
	};

	struct Joint :public Node {
		Node* m_ppC[2];
		const uint8_t* m_pKeyPtr; // should be equal to one of the ancestors
	};

public:

	struct Leaf :public Node {
	};


protected:
	Node* get_Root() const { return m_pRoot; }
	const uint8_t* get_NodeKey(const Node&) const;

	virtual Joint* CreateJoint() = 0;
	virtual Leaf* CreateLeaf() = 0;
	virtual uint8_t* GetLeafKey(const Leaf&) const = 0;
	virtual void DeleteJoint(Joint*) = 0;
	virtual void DeleteLeaf(Leaf*) = 0;

public:

	RadixTree();
	~RadixTree();

	void Clear();

	class CursorBase
	{
	protected:
		uint32_t m_nBits;
		uint32_t m_nPtrs;
		uint32_t m_nPosInLastNode;

		Node** const m_pp;

		static uint8_t get_BitRawStat(const uint8_t* p0, uint32_t nBit);

		uint8_t get_BitRaw(const uint8_t* p0) const;
		uint8_t get_Bit(const uint8_t* p0) const;

		friend class RadixTree;

	public:
		CursorBase(Node** pp) :m_pp(pp) {}

		Leaf& get_Leaf() const;
		void Invalidate();

		Node** get_pp() const { return m_pp; }
		uint32_t get_Depth() const { return m_nPtrs; }
	};

	template <uint32_t nKeyBits>
	class Cursor_T :public CursorBase
	{
		Node* m_ppBuf[nKeyBits + 1];
	public:
		Cursor_T() :CursorBase(m_ppBuf) {}
	};

	bool Goto(CursorBase& cu, const uint8_t* pKey, uint32_t nBits) const;

	Leaf* Find(CursorBase& cu, const uint8_t* pKey, uint32_t nBits, bool& bCreate);

	void Delete(CursorBase& cu);

	struct ITraveler
	{
		CursorBase* m_pCu; // set it to a valid cursor instance to get the cursor of the element during traverse.
		// Insert/Delete are not allowed. However it may be used for invalidation or etc.

		// optional min/max bounds
		const uint8_t* m_pBound[2];

		ITraveler()
			:m_pCu(NULL)
		{
			ZeroObject(m_pBound);
		}

		virtual bool OnLeaf(const Leaf&) = 0; // return false to stop iteration
	};

	bool Traverse(ITraveler&) const;

	size_t Count() const; // implemented via the whole tree traversing, shouldn't use frequently.

private:
	Node* m_pRoot;

	void DeleteNode(Node*);
	void ReplaceTip(CursorBase& cu, Node* pNew);
	bool Traverse(const Node&, ITraveler&) const;

	static int Cmp(const uint8_t* pKey, const uint8_t* pThreshold, uint32_t n0, uint32_t dn);
	static int Cmp1(uint8_t, const uint8_t* pThreshold, uint32_t n0);
};

class RadixHashTree
	:public RadixTree
{
public:

	struct MyJoint :public Joint {
		Merkle::Hash m_Hash;
	};

	void get_Hash(Merkle::Hash&);
	void get_Proof(Merkle::Proof&, const CursorBase&);

protected:
	// RadixTree
	virtual Joint* CreateJoint() override { return new MyJoint; }
	virtual void DeleteJoint(Joint* p) override { delete (MyJoint*) p; }

	const Merkle::Hash& get_Hash(Node&, Merkle::Hash&);

	virtual const Merkle::Hash& get_LeafHash(Node&, Merkle::Hash&) = 0;
};

class RadixHashOnlyTree
	:public RadixHashTree
{
public:

	// Just store hashes.

	struct MyLeaf :public Leaf
	{
		Merkle::Hash m_Hash;
	};

	typedef RadixTree::Cursor_T<ECC::nBits> Cursor;

	MyLeaf* Find(CursorBase& cu, const Merkle::Hash& key, bool& bCreate)
	{
		static_assert(sizeof(key.m_pData) << 3 == ECC::nBits, "");
		return (MyLeaf*) RadixTree::Find(cu, key.m_pData, ECC::nBits, bCreate);
	}

	~RadixHashOnlyTree() { Clear(); }

protected:
	virtual Leaf* CreateLeaf() override { return new MyLeaf; }
	virtual uint8_t* GetLeafKey(const Leaf& x) const override { return ((MyLeaf&) x).m_Hash.m_pData; }
	virtual void DeleteLeaf(Leaf* p) override { delete (MyLeaf*) p; }
	virtual const Merkle::Hash& get_LeafHash(Node& n, Merkle::Hash&) override { return ((MyLeaf&) n).m_Hash; }
};


class UtxoTree
	:public RadixHashTree
{
public:

	// This tree is different from RadixHashOnlyTree in 2 ways:
	//	1. Each key comes with a count (i.e. duplicates are allowed)
	//	2. We support "group search", i.e. all elements with a specified subkey. Given the UTXO commitment we can find all the counts and parameters.

	struct Key
	{
		static const uint32_t s_BitsCommitment = sizeof(ECC::uintBig) * 8 + 1; // curve point

		struct Data {
			ECC::Point m_Commitment;
			Height m_Maturity;
			Data& operator = (const Key&);
		};

		static const uint32_t s_Bits = s_BitsCommitment + sizeof(Height) * 8; // maturity
		static const uint32_t s_Bytes = (s_Bits + 7) >> 3;

		Key& operator = (const Data&);

		int cmp(const Key&) const;
		COMPARISON_VIA_CMP(Key)

		uint8_t m_pArr[s_Bytes];
	};

	struct Value
	{
		Input::Count m_Count;
		void get_Hash(Merkle::Hash&, const Key&) const;
	};

	struct MyLeaf :public Leaf
	{
		Key m_Key;
		Value m_Value;
	};

	typedef RadixTree::Cursor_T<Key::s_Bits> Cursor;

	MyLeaf* Find(CursorBase& cu, const Key& key, bool& bCreate)
	{
		return (MyLeaf*) RadixTree::Find(cu, key.m_pArr, key.s_Bits, bCreate);
	}

	~UtxoTree() { Clear(); }

    template<typename Archive>
    Archive& save(Archive& ar) const
	{
		Serializer<Archive> s(ar);
		SaveIntenral(s);
		return ar;
	}

    template<typename Archive>
    Archive& load(Archive& ar)
    {
		Serializer<Archive> s(ar);
		LoadIntenral(s);
		return ar;
	}


protected:
	virtual Leaf* CreateLeaf() override { return new MyLeaf; }
	virtual uint8_t* GetLeafKey(const Leaf& x) const override { return ((MyLeaf&) x).m_Key.m_pArr; }
	virtual void DeleteLeaf(Leaf* p) override { delete (MyLeaf*) p; }
	virtual const Merkle::Hash& get_LeafHash(Node&, Merkle::Hash&) override;

	struct ISerializer {
		virtual void Process(uint32_t&) = 0;
		virtual void Process(Key&) = 0;
		virtual void Process(Value&) = 0;
	};

	template <typename Archive>
	struct Serializer :public ISerializer {
		Archive& m_ar;
		Serializer(Archive& ar) :m_ar(ar) {}

		virtual void Process(uint32_t& n) override { m_ar & n; }
		virtual void Process(Key& k) override { m_ar & k.m_pArr; }
		virtual void Process(Value& v) override { m_ar & v.m_Count; }
	};

	void SaveIntenral(ISerializer&) const;
	void LoadIntenral(ISerializer&);
};

namespace Merkle
{
	struct Mmr
	{
		uint64_t m_Count;
		Mmr() :m_Count(0) {}

		void Append(const Hash&);

		void get_Hash(Hash&) const;
		void get_Proof(Proof&, uint64_t i) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;

	protected:
		bool get_HashForRange(Hash&, uint64_t n0, uint64_t n) const;

		virtual void LoadElement(Hash&, uint64_t nIdx, uint8_t nHeight) const = 0;
		virtual void SaveElement(const Hash&, uint64_t nIdx, uint8_t nHeight) = 0;
	};

	struct DistributedMmr
	{
		typedef uint64_t Key;

		uint64_t m_Count;
		Key m_kLast;

		DistributedMmr()
			:m_Count(0)
			,m_kLast(0)
		{
		}

		static uint32_t get_NodeSize(uint64_t n);

		void Append(Key, void* pBuf, const Hash&);

		void get_Hash(Hash&) const;
		void get_Proof(Proof&, uint64_t i) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;

	protected:
		// Get the data of the node referenced by Key. The data of this node will only be used until this function is called for another node.
		virtual const void* get_NodeData(Key) const = 0;
		virtual void get_NodeHash(Hash&, Key) const = 0;

		struct Impl;
	};

	class CompactMmr
	{
		// Only used to recalculate the new root hash after appending the element
		// Can't generate proofs.

		uint64_t m_Count;
		std::vector<Hash> m_vNodes; // rightmost branch, in top-down order

	public:

		CompactMmr() :m_Count(0) {}

		void Append(const Hash&);

		void get_Hash(Hash&) const;
		void get_PredictedHash(Hash&, const Hash& hvAppend) const;
	};
}

} // namespace beam
