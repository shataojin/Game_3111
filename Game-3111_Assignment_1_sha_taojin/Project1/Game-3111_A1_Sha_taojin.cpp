/** @file Week4-6-ShapeComplete.cpp
 *  @brief Shape Practice Solution.
 *
 *  Place all of the scene geometry in one big vertex and index buffer.
 * Then use the DrawIndexedInstanced method to draw one object at a time ((as the
 * world matrix needs to be changed between objects)
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */


#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class MyCastleApp : public D3DApp
{
public:
	MyCastleApp(HINSTANCE hInstance);
	MyCastleApp(const MyCastleApp& rhs) = delete;
	MyCastleApp& operator=(const MyCastleApp& rhs) = delete;
	~MyCastleApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		MyCastleApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

MyCastleApp::MyCastleApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	mMainWndCaption = L"sha_taojin_Castle";
	//give name to the windows
}

MyCastleApp::~MyCastleApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool MyCastleApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void MyCastleApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void MyCastleApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void MyCastleApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	if (mIsWireframe)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void MyCastleApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void MyCastleApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void MyCastleApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void MyCastleApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void MyCastleApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void MyCastleApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void MyCastleApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void MyCastleApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void MyCastleApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void MyCastleApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Create root CBVs.
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void MyCastleApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}


void MyCastleApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);//box
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(30.0f, 30.0f, 60, 40);//ground
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);//ball
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);//Cylinder

	///<summary>
	/// new 3d shapes build
	///</summary>

	GeometryGenerator::MeshData cone = geoGen.CreateCone(0.5f, 1.0f, 20, 20);
	GeometryGenerator::MeshData Pyramid_flat_head = geoGen.CreatePyramid_flat_head(1.5f,2.0f, 1.0f, 0);
	GeometryGenerator::MeshData Pyramid_pointed_head = geoGen.CreatePyramid_pointed_head(1.5f, 0.5f, 0);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0, 1.0f, 1.0, 3); 
	GeometryGenerator::MeshData pointed_cylinder = geoGen.Createpointed_cylinder(5.0f, 5.0f, 1);
	GeometryGenerator::MeshData gate = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);



	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();


	UINT coneVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT Pyramid_flat_headVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT Pyramid_pointed_headVertexOffset = Pyramid_flat_headVertexOffset + (UINT)Pyramid_flat_head.Vertices.size();
	UINT wedgeVertexOffset = Pyramid_pointed_headVertexOffset + (UINT)Pyramid_pointed_head.Vertices.size();
	UINT pointed_cylinderVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT gateVertexOffset = pointed_cylinderVertexOffset + (UINT)pointed_cylinder.Vertices.size();


	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();


	UINT coneIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT Pyramid_flat_headIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT Pyramid_pointed_headIndexOffset = Pyramid_flat_headIndexOffset + (UINT)Pyramid_flat_head.Indices32.size();
	UINT wedgeIndexOffset = Pyramid_pointed_headIndexOffset + (UINT)Pyramid_pointed_head.Indices32.size();
	UINT pointed_cylinderIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT gateIndexOffset = pointed_cylinderIndexOffset + (UINT)pointed_cylinder.Indices32.size();


	// Define the SubmeshGeometry that cover different
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;




	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry Pyramid_flat_headSubmesh;
	Pyramid_flat_headSubmesh.IndexCount = (UINT)Pyramid_flat_head.Indices32.size();
	Pyramid_flat_headSubmesh.StartIndexLocation = Pyramid_flat_headIndexOffset;
	Pyramid_flat_headSubmesh.BaseVertexLocation = Pyramid_flat_headVertexOffset;

	SubmeshGeometry Pyramid_pointed_headSubmesh;
	Pyramid_pointed_headSubmesh.IndexCount = (UINT)Pyramid_pointed_head.Indices32.size();
	Pyramid_pointed_headSubmesh.StartIndexLocation = Pyramid_pointed_headIndexOffset;
	Pyramid_pointed_headSubmesh.BaseVertexLocation = Pyramid_pointed_headVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry pointed_cylinderSubmesh;
	pointed_cylinderSubmesh.IndexCount = (UINT)pointed_cylinder.Indices32.size();
	pointed_cylinderSubmesh.StartIndexLocation = pointed_cylinderIndexOffset;
	pointed_cylinderSubmesh.BaseVertexLocation = pointed_cylinderVertexOffset;

	SubmeshGeometry gateSubmesh;
	gateSubmesh.IndexCount = (UINT)gate.Indices32.size();
	gateSubmesh.StartIndexLocation = gateIndexOffset;
	gateSubmesh.BaseVertexLocation = gateVertexOffset;

	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size()+


		cone.Vertices.size() +
		Pyramid_flat_head.Vertices.size() +
		Pyramid_pointed_head.Vertices.size() +
		wedge.Vertices.size() +
		gate.Vertices.size() +
		pointed_cylinder.Vertices.size() 
		;


	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;

	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gray);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}


	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gold);
	}
	for (size_t i = 0; i < Pyramid_flat_head.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = Pyramid_flat_head.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Red);
	}
	for (size_t i = 0; i < Pyramid_pointed_head.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = Pyramid_pointed_head.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Tan);
	}
	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Chocolate);
	}
	for (size_t i = 0; i < pointed_cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pointed_cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Pink);
	}

	for (size_t i = 0; i < gate.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = gate.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Cyan);
	}





	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));


	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(Pyramid_flat_head.GetIndices16()), std::end(Pyramid_flat_head.GetIndices16()));
	indices.insert(indices.end(), std::begin(Pyramid_pointed_head.GetIndices16()), std::end(Pyramid_pointed_head.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(pointed_cylinder.GetIndices16()), std::end(pointed_cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(gate.GetIndices16()), std::end(gate.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";


	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;



	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["Pyramid_flat_head"] = Pyramid_flat_headSubmesh;
	geo->DrawArgs["Pyramid_pointed_head"] = Pyramid_pointed_headSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["pointed_cylinder"] = pointed_cylinderSubmesh;
	geo->DrawArgs["gate"] = gateSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void MyCastleApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects.

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();

	opaquePsoDesc.VS =
	{
	 reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
	 mShaders["standardVS"]->GetBufferSize()
	};

	opaquePsoDesc.PS =
	{
	 reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
	 mShaders["opaquePS"]->GetBufferSize()
	};

	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	// PSO for opaque wireframe objects.

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}



void MyCastleApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size()));
	}
}



void MyCastleApp::BuildRenderItems()
{
	//ground
	UINT objIndex = 0;
	auto gridRitem = std::make_unique<RenderItem>();

	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = objIndex++;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	// 4 tower
	//each tower location
	float dx[4] = { 7.0f,7.0f, -7.0f, -7.0f }, dz[4] = { 7.0f, -7.0f, -7.0f, 7.0f };
	for (int i = 0; i < 4; ++i)
	{
		
		auto tower_base = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&tower_base->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(dx[i], 2.0f, dz[i]));
		tower_base->ObjCBIndex = objIndex++;
		tower_base->Geo = mGeometries["shapeGeo"].get();
		tower_base->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		tower_base->IndexCount = tower_base->Geo->DrawArgs["cylinder"].IndexCount;
		tower_base->StartIndexLocation = tower_base->Geo->DrawArgs["cylinder"].StartIndexLocation;
		tower_base->BaseVertexLocation = tower_base->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mAllRitems.push_back(std::move(tower_base));

		auto tower_middle = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&tower_middle->World, XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(dx[i], 4.5f, dz[i]));
		tower_middle->ObjCBIndex = objIndex++;
		tower_middle->Geo = mGeometries["shapeGeo"].get();
		tower_middle->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		tower_middle->IndexCount = tower_middle->Geo->DrawArgs["Pyramid_flat_head"].IndexCount;
		tower_middle->StartIndexLocation = tower_middle->Geo->DrawArgs["Pyramid_flat_head"].StartIndexLocation;
		tower_middle->BaseVertexLocation = tower_middle->Geo->DrawArgs["Pyramid_flat_head"].BaseVertexLocation;
		mAllRitems.push_back(std::move(tower_middle));

		auto tower_top = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&tower_top->World, XMMatrixTranslation(dx[i], 6.0f, dz[i]));
		tower_top->ObjCBIndex = objIndex++;
		tower_top->Geo = mGeometries["shapeGeo"].get();
		tower_top->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		tower_top->IndexCount = tower_top->Geo->DrawArgs["cone"].IndexCount;
		tower_top->StartIndexLocation = tower_top->Geo->DrawArgs["cone"].StartIndexLocation;
		tower_top->BaseVertexLocation = tower_top->Geo->DrawArgs["cone"].BaseVertexLocation;
		mAllRitems.push_back(std::move(tower_top));
	}

	//wall

	auto wall_one = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wall_one->World, XMMatrixScaling(14.0f, 3.0f, 1.5f) * XMMatrixTranslation(0.0, 1.0f, 7.0));
	wall_one->ObjCBIndex = objIndex++;
	wall_one->Geo = mGeometries["shapeGeo"].get();
	wall_one->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wall_one->IndexCount = wall_one->Geo->DrawArgs["box"].IndexCount;
	wall_one->StartIndexLocation = wall_one->Geo->DrawArgs["box"].StartIndexLocation;
	wall_one->BaseVertexLocation = wall_one->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wall_one));

	auto wall_two = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wall_two->World, XMMatrixScaling(14.0f, 3.0f, 1.5f) * XMMatrixTranslation(0.0, 1.0f, -7.0));
	wall_two->ObjCBIndex = objIndex++;
	wall_two->Geo = mGeometries["shapeGeo"].get();
	wall_two->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wall_two->IndexCount = wall_two->Geo->DrawArgs["box"].IndexCount;
	wall_two->StartIndexLocation = wall_two->Geo->DrawArgs["box"].StartIndexLocation;
	wall_two->BaseVertexLocation = wall_two->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wall_two));

	auto wall_three = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wall_three->World, XMMatrixScaling(14.0f, 3.0f, 1.5f) * XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(6.5f, 1.0f, 0.0));
	wall_three->ObjCBIndex = objIndex++;
	wall_three->Geo = mGeometries["shapeGeo"].get();
	wall_three->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wall_three->IndexCount = wall_three->Geo->DrawArgs["box"].IndexCount;
	wall_three->StartIndexLocation = wall_three->Geo->DrawArgs["box"].StartIndexLocation;
	wall_three->BaseVertexLocation = wall_three->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wall_three));

	auto wall_four = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wall_four->World, XMMatrixScaling(14.0f, 3.0f, 1.5f) * XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(-6.5f, 1.0f, 0.0f));
	wall_four->ObjCBIndex = objIndex++;
	wall_four->Geo = mGeometries["shapeGeo"].get();
	wall_four->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wall_four->IndexCount = wall_four->Geo->DrawArgs["box"].IndexCount;
	wall_four->StartIndexLocation = wall_four->Geo->DrawArgs["box"].StartIndexLocation;
	wall_four->BaseVertexLocation = wall_four->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wall_four));

	//wall fence
	float obj_loaction[4] = { -4.0f,-2.0f,2.0f,4.0f };

	for (int i = 0; i < 4; ++i)
	{
		auto wall_obj_1 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wall_obj_1->World, XMMatrixTranslation(obj_loaction[i], 3.0f, 7.0f));
		wall_obj_1->ObjCBIndex = objIndex++;
		wall_obj_1->Geo = mGeometries["shapeGeo"].get();
		wall_obj_1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wall_obj_1->IndexCount = wall_obj_1->Geo->DrawArgs["wedge"].IndexCount;
		wall_obj_1->StartIndexLocation = wall_obj_1->Geo->DrawArgs["wedge"].StartIndexLocation;
		wall_obj_1->BaseVertexLocation = wall_obj_1->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wall_obj_1));

		auto wall_obj_2= std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wall_obj_2->World, XMMatrixTranslation(obj_loaction[i], 3.0f, -7.0f));
		wall_obj_2->ObjCBIndex = objIndex++;
		wall_obj_2->Geo = mGeometries["shapeGeo"].get();
		wall_obj_2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wall_obj_2->IndexCount = wall_obj_2->Geo->DrawArgs["Pyramid_pointed_head"].IndexCount;
		wall_obj_2->StartIndexLocation = wall_obj_2->Geo->DrawArgs["Pyramid_pointed_head"].StartIndexLocation;
		wall_obj_2->BaseVertexLocation = wall_obj_2->Geo->DrawArgs["Pyramid_pointed_head"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wall_obj_2));

		auto wall_obj_3 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wall_obj_3->World, XMMatrixScaling(0.5f, 1.0f, 1.0f)* XMMatrixTranslation(-7.0f, 3.0f, obj_loaction[i]));
		wall_obj_3->ObjCBIndex = objIndex++;
		wall_obj_3->Geo = mGeometries["shapeGeo"].get();
		wall_obj_3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wall_obj_3->IndexCount = wall_obj_3->Geo->DrawArgs["box"].IndexCount;
		wall_obj_3->StartIndexLocation = wall_obj_3->Geo->DrawArgs["box"].StartIndexLocation;
		wall_obj_3->BaseVertexLocation = wall_obj_3->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wall_obj_3));

		auto wall_obj_4 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wall_obj_4->World, XMMatrixScaling(0.5f, 1.0f, 1.0f)*XMMatrixTranslation(7.0f, 3.0f, obj_loaction[i]));
		wall_obj_4->ObjCBIndex = objIndex++;
		wall_obj_4->Geo = mGeometries["shapeGeo"].get();
		wall_obj_4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wall_obj_4->IndexCount = wall_obj_4->Geo->DrawArgs["box"].IndexCount;
		wall_obj_4->StartIndexLocation = wall_obj_4->Geo->DrawArgs["box"].StartIndexLocation;
		wall_obj_4->BaseVertexLocation = wall_obj_4->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wall_obj_4));
	}

	auto base_building = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&base_building->World, XMMatrixScaling(0.5f, 1.5f, 0.5f)* XMMatrixScaling(7.5f, 1.5f, 7.5f));
	base_building->ObjCBIndex = objIndex++;
	base_building->Geo = mGeometries["shapeGeo"].get();
	base_building->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	base_building->IndexCount = base_building->Geo->DrawArgs["pointed_cylinder"].IndexCount;
	base_building->StartIndexLocation = base_building->Geo->DrawArgs["pointed_cylinder"].StartIndexLocation;
	base_building->BaseVertexLocation = base_building->Geo->DrawArgs["pointed_cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(base_building));

	auto baseRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&baseRitem->World, XMMatrixScaling(7.5f, 5.0f, 7.5f));
	baseRitem->ObjCBIndex = objIndex++;
	baseRitem->Geo = mGeometries["shapeGeo"].get();
	baseRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	baseRitem->IndexCount = baseRitem->Geo->DrawArgs["sphere"].IndexCount;
	baseRitem->StartIndexLocation = baseRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	baseRitem->BaseVertexLocation = baseRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mAllRitems.push_back(std::move(baseRitem));

	auto gate = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gate->World, XMMatrixScaling(4.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0, 1.0f, 7.0));
	gate->ObjCBIndex = objIndex++;
	gate->Geo = mGeometries["shapeGeo"].get();
	gate->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gate->IndexCount = gate->Geo->DrawArgs["gate"].IndexCount;
	gate->StartIndexLocation = gate->Geo->DrawArgs["gate"].StartIndexLocation;
	gate->BaseVertexLocation = gate->Geo->DrawArgs["gate"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gate));

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void MyCastleApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	// For each render item...

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.

		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;

		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());

		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

