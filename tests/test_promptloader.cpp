#include <gtest/gtest.h>
#include <QFile>
#include "core/promptloader.h"

// ---------------------------------------------------------------------
// loadFile() —— 从 RESOURCES_DIR（源码树真实文件）读取
//
// 这些测试依赖仓库里 app/resources/ 下的真实文件存在，是"集成测试"
// 性质而非纯单元测试，但 PromptLoader 本身就是个薄薄的文件读取封装，
// 测它的意义就在于"文件真的能被读到"，脱离真实文件测不出什么价值。
// ---------------------------------------------------------------------

TEST(PromptLoaderTest, LoadFile_ExistingLiteFile_ReturnsNonEmptyContent) {
    QString content = PromptLoader::loadFile("lite/enterprise.md");
    EXPECT_FALSE(content.isEmpty());
}

TEST(PromptLoaderTest, LoadFile_ExistingFullPersonalityFile_ReturnsNonEmptyContent) {
    QString content = PromptLoader::loadFile("full/enterprise_personality.md");
    EXPECT_FALSE(content.isEmpty());
}

TEST(PromptLoaderTest, LoadFile_ExistingFullQuotesFile_ReturnsNonEmptyContent) {
    QString content = PromptLoader::loadFile("full/enterprise_quotes.md");
    EXPECT_FALSE(content.isEmpty());
}

TEST(PromptLoaderTest, LoadFile_NonExistentFile_ReturnsEmptyStringNotCrash) {
    QString content = PromptLoader::loadFile("full/this_file_does_not_exist.md");
    EXPECT_TRUE(content.isEmpty());
}

TEST(PromptLoaderTest, LoadFile_ContentIsTrimmed) {
    // loadFile() 内部做了 trimmed()，返回内容首尾不应该有多余空白/换行
    QString content = PromptLoader::loadFile("lite/enterprise.md");
    ASSERT_FALSE(content.isEmpty());
    EXPECT_EQ(content, content.trimmed());
}

// ---------------------------------------------------------------------
// buildFullPersonaPrompt() / buildChatSystemPrompt()
// ---------------------------------------------------------------------

TEST(PromptLoaderTest, BuildFullPersonaPrompt_CombinesPersonalityAndQuotes) {
    QString personality = PromptLoader::loadFile("full/enterprise_personality.md");
    QString quotes = PromptLoader::loadFile("full/enterprise_quotes.md");
    ASSERT_FALSE(personality.isEmpty());
    ASSERT_FALSE(quotes.isEmpty());

    QString combined = PromptLoader::buildFullPersonaPrompt();
    EXPECT_TRUE(combined.contains(personality));
    EXPECT_TRUE(combined.contains(quotes));
    // 两段之间应该有间隔（拼接用的是 "\n\n"），不能是简单地字符串首尾相连
    EXPECT_TRUE(combined.contains(personality + "\n\n" + quotes)
                || combined.indexOf(quotes) > combined.indexOf(personality) + personality.length());
}

TEST(PromptLoaderTest, BuildChatSystemPrompt_Style0_ReturnsLiteContent) {
    QString lite = PromptLoader::loadFile("lite/enterprise.md");
    ASSERT_FALSE(lite.isEmpty());

    QString result = PromptLoader::buildChatSystemPrompt(0);
    EXPECT_EQ(result, lite);
}

TEST(PromptLoaderTest, BuildChatSystemPrompt_Style1_ReturnsFullPersonaContent) {
    QString expected = PromptLoader::buildFullPersonaPrompt();
    QString result = PromptLoader::buildChatSystemPrompt(1);
    EXPECT_EQ(result, expected);
}

TEST(PromptLoaderTest, BuildChatSystemPrompt_Style0And1_ProduceDifferentContent) {
    // 精简版和完整版应该是明显不同的两套内容，不能因为拼接逻辑写错
    // 导致两档退化成同一份东西
    QString lite = PromptLoader::buildChatSystemPrompt(0);
    QString full = PromptLoader::buildChatSystemPrompt(1);
    EXPECT_NE(lite, full);
}

// ---------------------------------------------------------------------
// qrc 兜底路径回归测试
//
// 这是最重要的一组测试：历史上 app.qrc 里 <file alias="..."> 没有带上
// full/lite 子目录前缀，跟 loadFile() 实际请求的相对路径对不上，导致
// RESOURCES_DIR 源码树路径不可用时（例如打包分发后）兜底静默失效，
// 人格设定直接丢失且没有任何报错提示。这里直接检查 qrc 资源路径和
// loadFile() 的请求路径是否一致，防止这个问题再次回归。
// ---------------------------------------------------------------------

TEST(PromptLoaderTest, QrcFallback_LitePathMatchesLoadFileRequest) {
    // loadFile("lite/enterprise.md") 失败时会尝试 ":/prompts/lite/enterprise.md"，
    // 这个资源路径必须真的存在于编译进二进制的 qrc 里
    QFile qrcFile(":/prompts/lite/enterprise.md");
    EXPECT_TRUE(qrcFile.exists())
        << "app.qrc 里的 alias 必须是 \"lite/enterprise.md\"（带子目录前缀），"
           "否则 loadFile() 的 qrc 兜底会静默失效";
}

TEST(PromptLoaderTest, QrcFallback_FullPersonalityPathMatchesLoadFileRequest) {
    QFile qrcFile(":/prompts/full/enterprise_personality.md");
    EXPECT_TRUE(qrcFile.exists())
        << "app.qrc 里的 alias 必须是 \"full/enterprise_personality.md\"（带子目录前缀）";
}

TEST(PromptLoaderTest, QrcFallback_FullQuotesPathMatchesLoadFileRequest) {
    QFile qrcFile(":/prompts/full/enterprise_quotes.md");
    EXPECT_TRUE(qrcFile.exists())
        << "app.qrc 里的 alias 必须是 \"full/enterprise_quotes.md\"（带子目录前缀）";
}

TEST(PromptLoaderTest, QrcFallback_ContentActuallyReadable) {
    // 光 exists() 还不够彻底，直接尝试打开读取，确保不是"文件存在但读不出内容"
    QFile qrcFile(":/prompts/lite/enterprise.md");
    ASSERT_TRUE(qrcFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QByteArray data = qrcFile.readAll();
    EXPECT_FALSE(data.isEmpty());
}
