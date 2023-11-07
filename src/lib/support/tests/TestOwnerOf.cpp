/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <lib/support/OwnerOf.h>

#include <gtest/gtest.h>

using namespace chip;

class Member
{
};

class Base
{
public:
    uint32_t Offset0;
    uint32_t Offset4;
    Member member;
};

TEST(TestOwnerOf, TestMemberOwner)
{
    Base base;
    Member * member = &base.member;
    EXPECT_TRUE(OwnerOf(member, &Base::member) == &base);
    EXPECT_TRUE(&OwnerOf(member, &Base::member)->member == member);
}
