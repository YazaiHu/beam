#pragma once

#include <stdint.h>
#include <string.h> // memcmp

#ifndef _countof
#	define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0]))
#endif // _countof

inline void memset0(void* p, size_t n) { memset(p, 0, n); }
bool memis0(const void* p, size_t n);

template <typename T>
inline void ZeroObject(T& x)
{
	memset0(&x, sizeof(x));
}

#define COMPARISON_VIA_CMP(class_name) \
	bool operator < (const class_name& x) const { return cmp(x) < 0; } \
	bool operator > (const class_name& x) const { return cmp(x) > 0; } \
	bool operator <= (const class_name& x) const { return cmp(x) <= 0; } \
	bool operator >= (const class_name& x) const { return cmp(x) >= 0; } \
	bool operator == (const class_name& x) const { return cmp(x) == 0; } \
	bool operator != (const class_name& x) const { return cmp(x) != 0; }

namespace ECC
{
	void InitializeContext(); // builds various generators. Necessary for commitments and signatures.
	// Not necessary for hashes, scalar and 'casual' point arithmetics

	struct Mode {
		enum Enum {
			Secure, // maximum security. Constant-time guarantee whenever possible, protection from side-channel attacks
			Fast
		};

		class Scope {
			const Enum m_PrevMode;
		public:
			Scope(Enum e);
			~Scope();
		};
	};

	struct Initializer {
		Initializer() {
			InitializeContext();
		}
	};

	// Syntactic sugar!
	enum Zero_ { Zero };
	enum Two_ { Two };

	struct Op
	{
		enum Sign {
			Plus,
			Minus,
			Mul,
			Div,
			Double
		};

		template <Sign, typename X>
		struct Unary {
			const X& x;
			Unary(const X& x_) :x(x_) {}
		};

		template <Sign, typename X, typename Y>
		struct Binary {
			const X& x;
			const Y& y;
			Binary(const X& x_, const Y& y_) :x(x_) ,y(y_) {}
		};
	};

	void SecureErase(void*, uint32_t);
	template <typename T> void SecureErase(T& t) { SecureErase(&t, sizeof(T)); }

	template <typename T>
	struct NoLeak
	{
		T V;
		~NoLeak() { SecureErase(V); }
	};

	template <uint32_t nBits_>
	struct uintBig_t
	{
		static_assert(!(7 & nBits_), "should be byte-aligned");

		// in Big-Endian representation
		uint8_t m_pData[nBits_ >> 3];

		constexpr size_t size() const
		{
			return sizeof(m_pData);
		}

		uintBig_t& operator = (Zero_)
		{
			ZeroObject(m_pData);
			return *this;
		}

		bool operator == (Zero_) const
		{
			return memis0(m_pData, sizeof(m_pData));
		}

		// from ordinal types (unsigned)
		template <typename T>
		uintBig_t& operator = (T x)
	{
			static_assert(sizeof(m_pData) >= sizeof(x), "too small");
			static_assert(T(-1) > 0, "must be unsigned");

			memset0(m_pData, sizeof(m_pData) - sizeof(x));

			for (int i = 0; i < sizeof(x); i++, x >>= 8)
				m_pData[_countof(m_pData) - 1 - i] = (uint8_t) x;

			return *this;
		}

		void Inc()
		{
			for (int i = 0; i < _countof(m_pData); i++)
				if (++m_pData[_countof(m_pData) - 1 - i])
					break;

		}

		void GenerateNonce(const uintBig_t& sk, const uintBig_t& msg, const uintBig_t* pMsg2, uint32_t nAttempt = 0); // implemented only for nBits_ = 256 bits

		int cmp(const uintBig_t& x) const { return memcmp(m_pData, x.m_pData, sizeof(m_pData)); }
		COMPARISON_VIA_CMP(uintBig_t)
	};

	static const uint32_t nBits = 256;
	typedef uintBig_t<nBits> uintBig;

	class Commitment;

	struct Scalar
	{
		static const uintBig s_Order;

		uintBig m_Value; // valid range is [0 .. s_Order)

		Scalar() {}
		template <typename T> explicit Scalar(const T& t) { *this = t; }

		bool IsValid() const;

		class Native;
		Scalar& operator = (const Native&);
		Scalar& operator = (const Zero_&);

		int cmp(const Scalar& x) const { return m_Value.cmp(x.m_Value); }
		COMPARISON_VIA_CMP(Scalar)
	};

	struct Point
	{
		static const uintBig s_FieldOrder; // The field order, it's different from the group order (a little bigger).

		uintBig	m_X; // valid range is [0 .. s_FieldOrder)
		bool	m_Y; // Flag for Y. Currently specifies if it's odd

		Point() {}
		template <typename T> Point(const T& t) { *this = t; }

		int cmp(const Point&) const;
		COMPARISON_VIA_CMP(Point)

		class Native;
		Point& operator = (const Native&);
		Point& operator = (const Point&);
		Point& operator = (const Commitment&);
	};

	struct Hash
	{
		typedef uintBig_t<256> Value;
		Value m_Value;

		class Processor;
	};

	typedef uint64_t Amount;

	struct Signature
	{
		Scalar m_e;
		Scalar m_k;

		bool IsValid(const Hash::Value& msg, const Point::Native& pk) const;

		// simple signature
		void Sign(const Hash::Value& msg, const Scalar::Native& sk);

		// multi-signature
		struct MultiSig;
		void CoSign(Scalar::Native& k, const Hash::Value& msg, const Scalar::Native& sk, const MultiSig&);

		int cmp(const Signature&) const;
		COMPARISON_VIA_CMP(Signature)

		void get_PublicNonce(Point::Native& pubNonce, const Point::Native& pk) const; // useful for verifications during multi-sig
		bool IsValidPartial(const Point::Native& pubNonce, const Point::Native& pk) const;

	private:
		static void get_Challenge(Scalar::Native&, const Point::Native&, const Hash::Value& msg);
	};

	struct Kdf
	{
		NoLeak<uintBig> m_Secret;
		void DeriveKey(Scalar::Native&, uint64_t nKeyIndex, uint32_t nFlags, uint32_t nExtra = 0) const;
	};

	struct InnerProduct
	{
		// Compact proof that the inner product of 2 vectors is a specified scalar.
		// Part of the bulletproof scheme
		//
		// Current implementation is 'fast' (i.e. not 'secure'), since the scheme isn't zero-knowledge wrt input vectors.
		// In bulletproof source vectors are blinded.

		static const uint32_t nDim = sizeof(Amount) << 3; // 64
		static const uint32_t nCycles = 6;
		static_assert(1 << nCycles == nDim, "");

		ECC::Point m_AB;				// orifinal commitment of both vectors
		ECC::Point m_pLR[nCycles][2];	// pairs of L,R values, per reduction  iteration
		ECC::Scalar m_pCondensed[2];	// remaining 1-dimension vectors

		static void get_Dot(Scalar::Native& res, const Scalar::Native* pA, const Scalar::Native* pB);

		// optional modifier for the used generators. Needed for the bulletproof.
		struct Modifier {
			const Scalar::Native* m_pMultiplier[2];
			Modifier() { ZeroObject(m_pMultiplier); }
		};

		void Create(const Scalar::Native* pA, const Scalar::Native* pB, const Modifier& = Modifier());
		bool IsValid(const Scalar::Native& dot, const Modifier& = Modifier()) const;

	private:
		struct Calculator;
	};

	namespace RangeProof
	{
		static const Amount s_MinimumValue = 1;

		struct Confidential
		{
			// Bulletproof scheme

			ECC::Point m_A;
			ECC::Point m_S;
			// <- y,z
			ECC::Point m_T1;
			ECC::Point m_T2;
			// <- x
			ECC::Scalar m_TauX;
			ECC::Scalar m_Mu;
			ECC::Scalar m_tDot;

			InnerProduct m_P_Tag; // contains commitment P - m_Mu*G

			void Create(const Scalar::Native& sk, Amount);
			bool IsValid(const Point&) const;

			int cmp(const Confidential&) const;
			COMPARISON_VIA_CMP(Confidential)
		};

		struct Public
		{
			Signature m_Signature;
			Amount m_Value;

			void Create(const Scalar::Native& sk); // amount should have been set
			bool IsValid(const Point&) const;

			int cmp(const Public&) const;
			COMPARISON_VIA_CMP(Public)

		private:
			void get_Msg(Hash::Value&) const;
		};
	}
}

