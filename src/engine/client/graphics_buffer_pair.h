#ifndef ENGINE_CLIENT_GRAPHICS_BUFFER_PAIR_H
#define ENGINE_CLIENT_GRAPHICS_BUFFER_PAIR_H

// 空资源允许空设备；仍持有资源时设备须有效。只在删除返回后清身份，异常继续传播。
template<typename TGraphics>
void DeleteGraphicsBufferPair(TGraphics *pGraphics, int &ContainerIndex, int &BufferObjectIndex)
{
	if(ContainerIndex != -1)
	{
		pGraphics->DeleteBufferContainer(ContainerIndex, true);
		ContainerIndex = -1;
		BufferObjectIndex = -1;
	}
	else if(BufferObjectIndex != -1)
	{
		pGraphics->DeleteBufferObject(BufferObjectIndex);
		BufferObjectIndex = -1;
	}
}

#endif
