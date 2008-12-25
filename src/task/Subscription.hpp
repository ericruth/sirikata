/*     Iridium Kernel -- Task scheduling system
 *  Subscription.hpp
 *
 *  Copyright (c) 2008, Patrick Reiter Horn
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Iridium nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef IRIDIUM_Subscription_HPP__
#define IRIDIUM_Subscription_HPP__

#include <typeinfo>
#include <string>
#include "HashMap.hpp"


namespace Iridium {

/**
 * Subscription.hpp -- Defines a SubscriptionId, as well as macros
 * to ease the creation of a unique identifier to go along with the
 * boost::function objects passed into EventManager and TimerQueue.
 */
namespace Task {

/**
 * An ID to allow comparing two callback functions.  It is up
 * to the creator to ensure uniqueness of SubscriptionId objects.
 *
 * @see TimerQueue
 * @see EventManager
 */
class SubscriptionId {
private:
	void *mThisPtr; ///< do not dereference
	const char *mClassId; ///< A compile-time constant, usually a class name or file or module name.
	std::string mUniqueId; ///< usually contains class or function name, and other arguments.

public:

	/// Equality comparison
	inline bool operator== (const SubscriptionId &other) const {
		if (mThisPtr != other.mThisPtr)
			return false;
		if (mClassId == NULL && other.mClassId == NULL)
			return (mUniqueId == other.mUniqueId);
		if (mClassId == NULL || other.mClassId == NULL)
			return false;
		// mClassId is not null
		if (strcmp(mClassId, other.mClassId))
			return false;
		return (mUniqueId == other.mUniqueId);
	}
	/// Ordering comparison
	inline bool operator< (const SubscriptionId &other) const {
		if (mThisPtr == other.mThisPtr) {
			if (mClassId == NULL && other.mClassId != NULL)
				return true;
			if (mClassId == NULL || other.mClassId == NULL)
				return false;
			// mClassId is not null
			int cmp = strcmp(mClassId, other.mClassId);
			if (cmp == 0) {
				return (mUniqueId < other.mUniqueId);
			} else {
				return cmp < 0;
			}
		} else {
			return (mThisPtr < other.mThisPtr);
		}
	}

	/**
	 * SubscriptionId is an ID that should be unique.  This ID is specified by
	 * the class, the class instance, and any other extra information needed
	 * in order to make this ID unique.
	 *
	 * @param thisPtr   Some identifying pointer for this class, or NULL.
	 * @param classId   A compile-time string (FULLY QUALIFIED class or filename)
	 * @param uniqueId  A specific string representing the request, usually
	 *                  the same as the corresponding SecondaryId.
	 */
	SubscriptionId(void *thisPtr,
				const char *classId,
				const std::string &uniqueId)
		: mThisPtr(thisPtr), mClassId(classId), mUniqueId(uniqueId) {
	}

	/**
	 * Create a null subscription ID.  A null subscription ID cannot be
	 * explicitly unsubscribed (and has greater efficiency when adding
	 * or removing event listeners)
	 */
	static inline SubscriptionId null() {
		return SubscriptionId(NULL, NULL, std::string());
	}

	/// Hasher functor to be used in a hash_map.
	struct Hasher {
		int operator() (const SubscriptionId &sid) const{
			return HASH<intptr_t>() ((intptr_t)sid.mThisPtr) * 43 +
				HASH<const char *>() (sid.mClassId) * 41 +
				HASH<const char *>() (sid.mUniqueId.c_str());
		}
	};
};

// TODO: provide a better (fool-proof) interface for these.

/// An ID for this instance of a class
#define CLASS_ID(cls, arg) \
	SubscriptionId(this, #cls, arg)

/// A generic ID.
#define GEN_ID(ptr, constname, id) \
	SubscriptionId(ptr, "[" constname "]", id)


}
}

#endif