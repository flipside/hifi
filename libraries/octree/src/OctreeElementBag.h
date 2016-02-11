//
//  OctreeElementBag.h
//  libraries/octree/src
//
//  Created by Brad Hefta-Gaub on 4/25/2013.
//  Copyright 2013 High Fidelity, Inc.
//
//  This class is used by the Octree:encodeTreeBitstream() functions to store elements and element data that need to be sent.
//  It's a generic bag style storage mechanism. But It has the property that you can't put the same element into the bag
//  more than once (in other words, it de-dupes automatically).
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_OctreeElementBag_h
#define hifi_OctreeElementBag_h

#include <unordered_set>
#include <shared/WeakPointerHash.h>

#include "OctreeElement.h"

class OctreeElementBag {
    using OctreeElementWeakPointerHash = WeakPointerHash<OctreeElement>;
    using OctreeElementWeakPointerEqual = WeakPointerEqual<OctreeElement>;
    using Bag = std::unordered_set<OctreeElementWeakPointer, OctreeElementWeakPointerHash, OctreeElementWeakPointerEqual>;
    
public:
    void insert(OctreeElementPointer element); // put a element into the bag

    OctreeElementPointer extract(); /// pull a element out of the bag (could come in any order) and if all of the
                                    /// elements have expired, a single null pointer will be returned

    bool isEmpty(); /// does the bag contain elements, 
                    /// if all of the contained elements are expired, they will not report as empty, and
                    /// a single last item will be returned by extract as a null pointer
    
    void deleteAll();

private:
    Bag _bagElements;
};

using OctreeElementExtraEncodeData = QMap<const OctreeElement*, void*>;

#endif // hifi_OctreeElementBag_h
