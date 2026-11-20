#include <game/editor/layer_selector.h>
#include <game/editor/mapitems/layer.h>

#include <gtest/gtest.h>

namespace
{
	class CHoverTarget : public CLayer
	{
	public:
		CHoverTarget() :
			CLayer(nullptr, LAYERTYPE_TILES)
		{
		}

		std::shared_ptr<CLayer> Duplicate() const override { return std::make_shared<CHoverTarget>(); }
		const char *TypeName() const override { return "test"; }
	};
}

TEST(EditorLayerHover, RemovedLayerIndexIsIgnored)
{
	auto pLayer = std::make_shared<CHoverTarget>();
	std::vector<std::shared_ptr<CLayer>> vpLayers{pLayer};
	const CHoverTile Hover(0, 0, pLayer);
	vpLayers.clear();
	EXPECT_EQ(Hover.ResolveLayer(vpLayers), nullptr);
}

TEST(EditorLayerHover, ReplacementLayerAtSameIndexIsIgnored)
{
	auto pLayer = std::make_shared<CHoverTarget>();
	std::vector<std::shared_ptr<CLayer>> vpLayers{pLayer};
	const CHoverTile Hover(0, 0, pLayer);
	vpLayers[0] = std::make_shared<CHoverTarget>();
	EXPECT_EQ(Hover.ResolveLayer(vpLayers), nullptr);
}

TEST(EditorLayerHover, UnchangedLayerAtRecordedIndexIsSelectable)
{
	auto pLayer = std::make_shared<CHoverTarget>();
	const std::vector<std::shared_ptr<CLayer>> vpLayers{pLayer};
	const CHoverTile Hover(0, 0, pLayer);
	EXPECT_EQ(Hover.ResolveLayer(vpLayers), pLayer);
}

TEST(EditorLayerHover, CachedHoverDoesNotKeepDeletedLayerAlive)
{
	auto pLayer = std::make_shared<CHoverTarget>();
	const std::weak_ptr<CLayer> WeakLayer = pLayer;
	const CHoverTile Hover(0, 0, pLayer);
	pLayer.reset();
	EXPECT_TRUE(WeakLayer.expired());
	EXPECT_EQ(Hover.ResolveLayer({}), nullptr);
}
