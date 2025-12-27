#include <gtest/gtest.h>

/**
 * @brief 一个简单的加法函数
 */
int simple_add(int a, int b)
{
    return a + b;
}

/**
 * @brief 计算阶乘的函数
 */
int factorial(int n)
{
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

// --- GTest 测试用例 ---

/**
 * @test 测试简单加法
 */
TEST(MathDemoTest, AddFunction)
{
    EXPECT_EQ(simple_add(1, 1), 2);
    EXPECT_EQ(simple_add(-1, 1), 0);
    EXPECT_EQ(simple_add(100, 200), 300);
}

/**
 * @test 测试阶乘逻辑
 */
TEST(MathDemoTest, FactorialFunction)
{
    EXPECT_EQ(factorial(0), 1);
    EXPECT_EQ(factorial(1), 1);
    EXPECT_EQ(factorial(5), 120); // 5! = 120
}

/**
 * @test 故意失败的测试 (你可以取消注释来观察 GTest 报错)
 */
/*
TEST(MathDemoTest, IntentionallyFail) {
    EXPECT_EQ(simple_add(1, 1), 3);
}
*/

// int main(int argc, char **argv)
// {
//     ::testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }