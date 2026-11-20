#include <game/map/render_component.h>

#include <gtest/gtest.h>

TEST(RenderComponent, NewComponentHasNoBorrowedInterfaces)
{
	CRenderComponent Component;
	EXPECT_EQ(Component.Graphics(), nullptr);
	EXPECT_EQ(Component.TextRender(), nullptr);
	EXPECT_EQ(Component.RenderMap(), nullptr);
	const CRenderComponent &ReadOnly = Component;
	EXPECT_EQ(ReadOnly.Graphics(), nullptr);
	EXPECT_EQ(ReadOnly.RenderMap(), nullptr);
}
