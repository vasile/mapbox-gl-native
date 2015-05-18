#include "../fixtures/util.hpp"
#include "mock_file_source.hpp"
#include "mock_view.hpp"

#include <mbgl/geometry/glyph_atlas.hpp>
#include <mbgl/geometry/sprite_atlas.hpp>
#include <mbgl/map/environment.hpp>
#include <mbgl/map/map_data.hpp>
#include <mbgl/map/resource_loader.hpp>
#include <mbgl/map/transform_state.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/text/glyph_store.hpp>
#include <mbgl/util/io.hpp>
#include <mbgl/util/run_loop.hpp>
#include <mbgl/util/texture_pool.hpp>
#include <mbgl/util/thread.hpp>

using namespace mbgl;

namespace {

class MockMapContext : public ResourceLoader::Observer {
public:
    MockMapContext(uv_loop_t* loop,
                   View& view,
                   FileSource& fileSource,
                   const std::function<void()>& successCallback,
                   const std::function<void()>& failureCallback)
        : env_(fileSource),
          envScope_(env_, ThreadType::Map, "Map"),
          data_(view, MapMode::Still),
          glyphStore_(util::make_unique<GlyphStore>(loop, env_)),
          glyphAtlas_(util::make_unique<GlyphAtlas>(1024, 1024)),
          spriteAtlas_(util::make_unique<SpriteAtlas>(512, 512)),
          texturePool_(util::make_unique<TexturePool>()),
          style_(util::make_unique<Style>()),
          resourceLoader_(util::make_unique<ResourceLoader>()),
          asyncUpdate(util::make_unique<uv::async>(loop, [this] { update(); })),
          successCallback_(successCallback),
          failureCallback_(failureCallback) {
        asyncUpdate->unref();

        data_.transform.resize(1000, 1000, 1.0, 1000, 1000);
        data_.transform.setLatLngZoom({0, 0}, 16);

        const std::string style = util::read_file("test/fixtures/resources/style.json");
        style_->loadJSON(reinterpret_cast<const uint8_t *>(style.c_str()));

        glyphStore_->setURL(style_->glyph_url);

        resourceLoader_->setGlyphStore(glyphStore_.get());
        resourceLoader_->setObserver(this);
        resourceLoader_->setStyle(style_.get());
    }

    ~MockMapContext() {
        resourceLoader_.reset();
        style_.reset();
        texturePool_.reset();
        spriteAtlas_.reset();
        glyphAtlas_.reset();
        glyphStore_.reset();

        env_.performCleanup();
    }

    void update() {
        const auto now = Clock::now();

        data_.setAnimationTime(now);
        data_.transform.updateTransitions(now);

        transformState_ = data_.transform.currentState();

        resourceLoader_->update(
            data_, transformState_, *glyphAtlas_, *spriteAtlas_, *texturePool_);
    }

    // ResourceLoader::Observer implementation.
    void onTileDataChanged() override {
        util::ptr<Sprite> sprite = resourceLoader_->getSprite();
        if (sprite && sprite->isLoaded() && style_->isLoaded()) {
            successCallback_();
        }

        asyncUpdate->send();
    };

    void onResourceLoadingFailed() override {
        failureCallback_();
    }

private:
    Environment env_;
    EnvironmentScope envScope_;

    MapData data_;
    TransformState transformState_;

    std::unique_ptr<GlyphStore> glyphStore_;
    std::unique_ptr<GlyphAtlas> glyphAtlas_;
    std::unique_ptr<SpriteAtlas> spriteAtlas_;
    std::unique_ptr<TexturePool> texturePool_;
    std::unique_ptr<Style> style_;
    std::unique_ptr<ResourceLoader> resourceLoader_;

    std::unique_ptr<uv::async> asyncUpdate;

    std::function<void()> successCallback_;
    std::function<void()> failureCallback_;
};

}

class ResourceLoaderTest : public ::testing::TestWithParam<std::string> {
};

TEST_P(ResourceLoaderTest, RequestFailure) {
    util::RunLoop loop(uv_default_loop());

    const std::string param(GetParam());

    MockView view;
    MockFileSource fileSource(param);

    bool success = false, failure = false;

    auto successCallback = [&loop, &success] { success = true; loop.stop(); };
    auto failureCallback = [&loop, &failure] { failure = true; loop.stop(); };

    util::Thread<MockMapContext> context(
        "Map", util::ThreadPriority::Regular, view, fileSource, successCallback, failureCallback);

    uv_run(loop.get(), UV_RUN_DEFAULT);

    if (param.empty()) {
        EXPECT_TRUE(success);
        EXPECT_FALSE(failure);
    } else {
        EXPECT_FALSE(success);
        EXPECT_TRUE(failure);
    }
}

INSTANTIATE_TEST_CASE_P(ResourceLoader, ResourceLoaderTest,
    ::testing::Values("", "source.json", "sprite.json", "sprite.png", "vector.pbf", "glyphs.pbf"));
