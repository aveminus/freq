#include "blocklayout.h"

#include "collection.h"
#include "signal/operation.h"

namespace Heightmap {

BlockLayout::
        BlockLayout(int texels_per_row, int texels_per_column, SampleRate fs, int mipmaps)
    :
        texels_per_column_( texels_per_column ),
        texels_per_row_( texels_per_row ),
        sample_rate_( fs ),
        mipmaps_( mipmaps )
{
    EXCEPTION_ASSERT_LESS( 1, texels_per_row );
    EXCEPTION_ASSERT_LESS( 1, texels_per_column );
    EXCEPTION_ASSERT_LESS( 0, fs );
    EXCEPTION_ASSERT_LESS( 0, fs );
    EXCEPTION_ASSERT_LESS_OR_EQUAL( 0, mipmaps );
}


bool BlockLayout::
        operator==(const BlockLayout& b) const
{
    return texels_per_column_ == b.texels_per_column_ &&
           texels_per_row_ == b.texels_per_row_ &&
           sample_rate_ == b.sample_rate_ &&
           mipmaps_ == b.mipmaps_;
}


bool BlockLayout::
        operator!=(const BlockLayout& b) const
{
    return !(*this == b);
}


std::string BlockLayout::
        toString() const
{
    return (boost::format("BlockSize(%1%, %2%, %3%)")
       % texels_per_row_
       % texels_per_column_
       % sample_rate_).str();
}

} // namespace Heightmap


namespace Heightmap {

void BlockLayout::
        test()
{
    // It should describe the size in texels of blocks in a heightmap
    {
        BlockLayout b(12, 34, 5);
        EXCEPTION_ASSERT_EQUALS(b.texels_per_block (), 12*34);
        EXCEPTION_ASSERT_EQUALS(b.texels_per_row (), 12);
        EXCEPTION_ASSERT_EQUALS(b.texels_per_column (), 34);
        EXCEPTION_ASSERT_EQUALS(b.sample_rate (), 5);
        EXCEPTION_ASSERT_EQUALS((boost::format("%1%")%b).str(), "BlockSize(12, 34, 5)");
        EXCEPTION_ASSERT_EQUALS(b, BlockLayout(12, 34, 5));
        EXCEPTION_ASSERT(b!=BlockLayout(12, 34, 4));
        EXCEPTION_ASSERT(b!=BlockLayout(11, 34, 5));
        EXCEPTION_ASSERT(b!=BlockLayout(12, 35, 5));
        EXCEPTION_ASSERT(b==BlockLayout(12, 34, 5));
    }

    // It should be immutable POD
    {
        // "immutable" is implemented by no setters, only getters
        // "POD" implies that two instances from the same parameters are bitwise identical
        BlockLayout b1(12, 34, 5);
        BlockLayout b2(12, 34, 5);
        EXCEPTION_ASSERT_EQUALS(0, memcmp (&b1, &b2, sizeof(BlockLayout )));
    }
}

} // namespace Heightmap
