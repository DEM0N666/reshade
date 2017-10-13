/**
 * Copyright (C) 2014 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#include "log.hpp"
#include "d3d11_runtime.hpp"
#include "d3d11_effect_compiler.hpp"
#include "lexer.hpp"
#include "input.hpp"
#include "resource_loading.hpp"
#include "..\deps\imgui\imgui.h"
#include <algorithm>
#include <map>
#include <atlbase.h>


typedef bool (__stdcall *SK_ReShade_PresentCallback_pfn)(void *user);
typedef void (__stdcall *SK_ReShade_OnCopyResourceD3D11_pfn)(void* user, ID3D11Resource *&dest, ID3D11Resource *&source);
typedef void (__stdcall *SK_ReShade_OnClearDepthStencilViewD3D11_pfn)(void* user, ID3D11DepthStencilView *&depthstencil);
typedef void (__stdcall *SK_ReShade_OnGetDepthStencilViewD3D11_pfn)(void* user, ID3D11DepthStencilView *&depthstencil);
typedef void (__stdcall *SK_ReShade_OnSetDepthStencilViewD3D11_pfn)(void* user, ID3D11DepthStencilView *&depthstencil);
typedef void (__stdcall *SK_ReShade_OnDrawD3D11_pfn)(void* user, ID3D11DeviceContext *context, unsigned int vertices);

__declspec (dllimport)
void
SK_ReShade_InstallPresentCallback (SK_ReShade_PresentCallback_pfn fn, void* user);

__declspec (dllimport)
void
SK_ReShade_InstallDrawCallback (SK_ReShade_OnDrawD3D11_pfn fn, void* user);

__declspec (dllimport)
void
SK_ReShade_InstallSetDepthStencilViewCallback (SK_ReShade_OnSetDepthStencilViewD3D11_pfn fn, void* user);

__declspec (dllimport)
void
SK_ReShade_InstallGetDepthStencilViewCallback (SK_ReShade_OnGetDepthStencilViewD3D11_pfn fn, void* user);

__declspec (dllimport)
void
SK_ReShade_InstallClearDepthStencilViewCallback (SK_ReShade_OnClearDepthStencilViewD3D11_pfn fn, void* user);

__declspec (dllimport)
void
SK_ReShade_InstallCopyResourceCallback (SK_ReShade_OnCopyResourceD3D11_pfn fn, void* user);

struct explict_draw_s
{
  void*                   ptr;
  ID3D11RenderTargetView* pRTV;
  bool                    pass  = false;
  int                     calls = 0;
} explicit_draw;


bool
__stdcall
SK_ReShade_PresentCallbackD3D11 (void *user)
{
  const auto runtime =
    (reshade::d3d11::d3d11_runtime *)((explict_draw_s *)user)->ptr;

  if (! explicit_draw.pass)
  {
    //explicit_draw.calls = ((explict_draw_s *)user)->calls;
    explicit_draw.pass  = true;
    runtime->on_present ();
    explicit_draw.pass = false;
  }

  return true;
}

void
SK_ReShade_OnCopyResourceCallbackD3D11 (void* user, ID3D11Resource *&dest, ID3D11Resource *&source)
{
  ((reshade::d3d11::d3d11_runtime *)user)->on_copy_resource (dest, source);
}

void
SK_ReShade_OnClearDepthStencilViewD3D11 (void* user, ID3D11DepthStencilView *&depthstencil)
{
  ((reshade::d3d11::d3d11_runtime *)user)->on_clear_depthstencil_view (depthstencil);
}

void
SK_ReShade_OnGetDepthStencilViewD3D11 (void* user, ID3D11DepthStencilView *&depthstencil)
{
  ((reshade::d3d11::d3d11_runtime *)user)->on_get_depthstencil_view (depthstencil);
}

void
SK_ReShade_OnSetDepthStencilViewD3D11 (void* user, ID3D11DepthStencilView *&depthstencil)
{
  ((reshade::d3d11::d3d11_runtime *)user)->on_set_depthstencil_view (depthstencil);
}

void
SK_ReShade_OnDrawD3D11 (void* user, ID3D11DeviceContext *context, unsigned int vertices)
{
  ((reshade::d3d11::d3d11_runtime *)user)->on_draw_call (context, vertices);
}


_Return_type_success_ (nullptr)
IUnknown*
SK_COM_ValidateRelease (IUnknown** ppObj)
{
  if ((! ppObj) || (! ReadPointerAcquire ((volatile LPVOID *)ppObj)))
    return nullptr;
  
  ULONG refs =
    (*ppObj)->Release ();
  
  assert (refs == 0);
  
  if (refs == 0)
  {
    InterlockedExchangePointer ((void **)ppObj, nullptr);
  }

  return *ppObj;
}

IMGUI_API
void
ImGui_ImplDX11_RenderDrawLists (ImDrawData* draw_data);

#if 0

struct SK_DisjointTimerQueryD3D11
{
  volatile ID3D11Query* async  = nullptr;
  volatile LONG         active = false;

  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT last_results = { };
};

struct SK_TimerQueryD3D11
{
  volatile ID3D11Query* async  = nullptr;
  volatile LONG         active = FALSE;

  UINT64 last_results = { };
};

struct SK_DisjointTimerQueryD3D11
{

  ID3D11Query* async  = nullptr;
  bool         active = false;

  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT last_results = { };
};

struct SK_TimerQueryD3D11
{
  ID3D11Query* async  = nullptr;
  bool         active = false;

  UINT64 last_results = { };
};

  static SK_DisjointTimerQueryD3D11 disjoint_query;

  struct duration_s
  {
    // Timestamp at beginning
    SK_TimerQueryD3D11 start;

    // Timestamp at end
    SK_TimerQueryD3D11 end;
  };
  std::vector <duration_s> timers;

  // Cumulative runtime of all timers after the disjoint query
  //   is finished and reading these results would not stall
  //     the pipeline
  UINT64                            runtime_ticks   = 0ULL;
  double                            runtime_ms      = 0.0;
  double                            last_runtime_ms = 0.0;
#endif

namespace reshade::d3d11
{
	extern DXGI_FORMAT make_format_srgb     (DXGI_FORMAT format);
	extern DXGI_FORMAT make_format_normal   (DXGI_FORMAT format);
	extern DXGI_FORMAT make_format_typeless (DXGI_FORMAT format);

	d3d11_runtime::d3d11_runtime (ID3D11Device *device, IDXGISwapChain *swapchain) : runtime     (device->GetFeatureLevel ()),
	                                                                                 _device     (device),
	                                                                                 _swapchain  (swapchain),
	                                                                                 _stateblock (device)
	{
		assert (device    != nullptr);
		assert (swapchain != nullptr);

		_device->GetImmediateContext (&_immediate_context);

		HRESULT                hr           = E_FAIL;
		DXGI_ADAPTER_DESC      adapter_desc = { };
		com_ptr <IDXGIDevice>  dxgidevice   = nullptr;
		com_ptr <IDXGIAdapter> dxgiadapter  = nullptr;

		hr =
			_device->QueryInterface (&dxgidevice);

		assert (SUCCEEDED (hr));

		hr =
			dxgidevice->GetAdapter (&dxgiadapter);

		assert (SUCCEEDED (hr));

		hr =
			dxgiadapter->GetDesc (&adapter_desc);

		assert (SUCCEEDED (hr));

		_vendor_id = adapter_desc.VendorId;
		_device_id = adapter_desc.DeviceId;
	}

	bool
	d3d11_runtime::init_backbuffer_texture (void)
	{
		// Get back buffer texture
		HRESULT hr =
			_swapchain->GetBuffer (0, IID_PPV_ARGS (&_backbuffer));

		assert (SUCCEEDED (hr));

		D3D11_TEXTURE2D_DESC texdesc = { };

		texdesc.Width      = _width;
		texdesc.Height     = _height;
		texdesc.ArraySize  = texdesc.MipLevels = 1;
		texdesc.Format     = make_format_typeless (_backbuffer_format);
		texdesc.SampleDesc = { 1, 0 };
		texdesc.Usage      = D3D11_USAGE_DEFAULT;
		texdesc.BindFlags  = D3D11_BIND_RENDER_TARGET;

		OSVERSIONINFOEX verinfo_windows7 = {
			sizeof (OSVERSIONINFOEX), 6, 1
		};

		const bool is_windows7 =
			VerifyVersionInfo ( &verinfo_windows7,  VER_MAJORVERSION | VER_MINORVERSION,
														VerSetConditionMask ( VerSetConditionMask (0, VER_MAJORVERSION, VER_EQUAL),
																							VER_MINORVERSION, VER_EQUAL )
												) != FALSE;
    
		//if ( _is_multisampling_enabled ||
		//	   make_format_normal (_backbuffer_format) != _backbuffer_format ||
		//	   (! is_windows7) )
		//{
		  hr =
		  	_device->CreateTexture2D (&texdesc, nullptr, &_backbuffer_resolved);

		  if (FAILED (hr))
		  {
		  	LOG (ERROR)          << "Failed to create back buffer resolve texture ("
		  		"Width = "         << texdesc.Width              << ", "
		  		"Height = "        << texdesc.Height             << ", "
		  		"Format = "        << texdesc.Format             << ", "
		  		"SampleCount = "   << texdesc.SampleDesc.Count   << ", "
		  		"SampleQuality = " << texdesc.SampleDesc.Quality << ")! HRESULT is '" << std::hex << hr << std::dec << "'.";

		  	return false;
		  }

		  hr =
		  	_device->CreateRenderTargetView (_backbuffer.get (), nullptr, &_backbuffer_rtv [2]);

		  assert (SUCCEEDED (hr));
		//}
    if (! ( _is_multisampling_enabled ||
    	      make_format_normal (_backbuffer_format) != _backbuffer_format ||
    	      (! is_windows7) )
       )
    {
      _backbuffer_resolved = _backbuffer;
    }

		// Create back buffer shader texture
		texdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		hr =
			_device->CreateTexture2D (&texdesc, nullptr, &_backbuffer_texture);

		if (SUCCEEDED (hr))
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srvdesc = { };

			srvdesc.Format              = make_format_normal (texdesc.Format);
			srvdesc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvdesc.Texture2D.MipLevels = texdesc.MipLevels;

			if (SUCCEEDED(hr))
			{
				hr =
					_device->CreateShaderResourceView (_backbuffer_texture.get (), &srvdesc, &_backbuffer_texture_srv [0]);
			}
			else
			{
				LOG(ERROR) << "Failed to create back buffer texture resource view ("
					"Format = " << srvdesc.Format << ")! HRESULT is '" << std::hex << hr << std::dec << "'.";
			}

			srvdesc.Format = make_format_srgb (texdesc.Format);

			if (SUCCEEDED(hr))
			{
				hr =
					_device->CreateShaderResourceView (_backbuffer_texture.get (), &srvdesc, &_backbuffer_texture_srv [1]);
			}
			else
			{
				LOG(ERROR) << "Failed to create back buffer SRGB texture resource view ("
				              "Format = " << srvdesc.Format << ")! HRESULT is '" << std::hex << hr << std::dec << "'.";
			}
		}
		else
		{
			LOG (ERROR)          << "Failed to create back buffer texture ("
				"Width = "         << texdesc.Width              << ", "
				"Height = "        << texdesc.Height             << ", "
				"Format = "        << texdesc.Format             << ", "
				"SampleCount = "   << texdesc.SampleDesc.Count   << ", "
				"SampleQuality = " << texdesc.SampleDesc.Quality << ")! HRESULT is '" << std::hex << hr << std::dec << "'.";
		}

		if (FAILED (hr))
		{
			return false;
		}

		D3D11_RENDER_TARGET_VIEW_DESC rtdesc = { };

		rtdesc.Format        = make_format_normal (texdesc.Format);
		rtdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

		hr =
			_device->CreateRenderTargetView (_backbuffer_resolved.get (), &rtdesc, &_backbuffer_rtv [0]);

		if (FAILED (hr))
		{
			LOG (ERROR) << "Failed to create back buffer render target ("
				             "Format = " << rtdesc.Format << ")! HRESULT is '" << std::hex << hr << std::dec << "'.";
			return false;
		}

		rtdesc.Format = make_format_srgb (texdesc.Format);

		hr =
			_device->CreateRenderTargetView (_backbuffer_resolved.get (), &rtdesc, &_backbuffer_rtv [1]);

		if (FAILED (hr))
		{
			LOG (ERROR) << "Failed to create back buffer SRGB render target ("
				             "Format = " << rtdesc.Format << ")! HRESULT is '" << std::hex << hr << std::dec << "'.";
			return false;
		}

		{
			const resources::data_resource vs =
				resources::load_data_resource (IDR_RCDATA1);

			hr =
				_device->CreateVertexShader (vs.data, vs.data_size, nullptr, &_copy_vertex_shader);

			if (FAILED (hr))
			{
				return false;
			}

			const resources::data_resource ps =
				resources::load_data_resource (IDR_RCDATA2);

			hr =
				_device->CreatePixelShader (ps.data, ps.data_size, nullptr, &_copy_pixel_shader);

			if (FAILED (hr))
			{
				return false;
			}
		}

		{
			const D3D11_SAMPLER_DESC desc = {
				D3D11_FILTER_MIN_MAG_MIP_POINT,
				D3D11_TEXTURE_ADDRESS_CLAMP,
				D3D11_TEXTURE_ADDRESS_CLAMP,
				D3D11_TEXTURE_ADDRESS_CLAMP
			};

			hr =
				_device->CreateSamplerState (&desc, &_copy_sampler);

			if (FAILED (hr))
			{
				return false;
			}
		}

		return true;
	}

	bool
	d3d11_runtime::init_default_depth_stencil (void)
	{
		const D3D11_TEXTURE2D_DESC texdesc = {
			_width,
			_height,
			1, 1,
			DXGI_FORMAT_D24_UNORM_S8_UINT,
			{ 1, 0 },
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_DEPTH_STENCIL
		};

		com_ptr <ID3D11Texture2D> depth_stencil_texture = nullptr;

		HRESULT hr =
			_device->CreateTexture2D (&texdesc, nullptr, &depth_stencil_texture);

		if (FAILED (hr))
		{
			LOG (ERROR)          << "Failed to create depth stencil texture ("
				"Width = "         << texdesc.Width              << ", "
				"Height = "        << texdesc.Height             << ", "
				"Format = "        << texdesc.Format             << ", "
				"SampleCount = "   << texdesc.SampleDesc.Count   << ", "
				"SampleQuality = " << texdesc.SampleDesc.Quality << ")! HRESULT is '" << std::hex << hr << std::dec << "'.";
			return false;
		}

		hr =
			_device->CreateDepthStencilView (depth_stencil_texture.get (), nullptr, &_default_depthstencil);

		return SUCCEEDED (hr);
	}

	bool
	d3d11_runtime::init_fx_resources (void)
	{
		D3D11_RASTERIZER_DESC desc = { };

		desc.FillMode             = D3D11_FILL_SOLID;
		desc.CullMode             = D3D11_CULL_NONE;
		desc.DepthClipEnable      = TRUE;
		desc.SlopeScaledDepthBias = 0.0f;
		desc.DepthBiasClamp       = 0.0f;

		return
			SUCCEEDED (_device->CreateRasterizerState (&desc, &_effect_rasterizer_state));
	}

	bool
	d3d11_runtime::on_init (const DXGI_SWAP_CHAIN_DESC &desc)
	{
		_width                    = desc.BufferDesc.Width;
		_height                   = desc.BufferDesc.Height;
		_backbuffer_format        = desc.BufferDesc.Format;
		_is_multisampling_enabled = desc.SampleDesc.Count > 1;

		if ( (! init_backbuffer_texture    ()) ||
			   (! init_default_depth_stencil ()) ||
			   (! init_fx_resources          ()) )
		{
			return false;
		}

		// Clear reference count to make UnrealEngine happy
		_backbuffer->Release ();

		return runtime::on_init ();
	}

	void
	d3d11_runtime::on_reset (void)
	{
		if (! is_initialized ())
		{
			return;
		}

		runtime::on_reset ();

		// Reset reference count to make UnrealEngine happy
		_backbuffer->AddRef ();

		// Destroy resources
		_backbuffer.reset                   ();
		_backbuffer_resolved.reset          ();
		_backbuffer_texture.reset           ();
		_backbuffer_texture_srv [0].reset   ();
		_backbuffer_texture_srv [1].reset   ();
		_backbuffer_rtv [0].reset           ();
		_backbuffer_rtv [1].reset           ();
		_backbuffer_rtv [2].reset           ();

		_depthstencil.reset             ();
		_depthstencil_replacement.reset ();
		_depthstencil_texture.reset         ();
		_depthstencil_texture_srv.reset     ();

		_default_depthstencil.reset         ();
		_copy_vertex_shader.reset           ();
		_copy_pixel_shader.reset            ();
		_copy_sampler.reset                 ();

		_effect_rasterizer_state.reset      ();
	}

	void
  d3d11_runtime::on_reset_effect (void)
	{
		runtime::on_reset_effect ();

		for (auto it : _effect_sampler_states)
		{
			it->Release ();
		}

		_effect_sampler_descs.clear  ();
		_effect_sampler_states.clear ();
		_constant_buffers.clear      ();

		_effect_shader_resources.resize (3);
		_effect_shader_resources [0] = _backbuffer_texture_srv [0].get ();
		_effect_shader_resources [1] = _backbuffer_texture_srv [1].get ();
		_effect_shader_resources [2] = _depthstencil_texture_srv.get   ();
	}

	void
	d3d11_runtime::on_present (void)
	{
    static int  last_calls = 0;
    static bool first      = true;

    if (is_initialized ())
    {
      SK_ReShade_InstallPresentCallback               (SK_ReShade_PresentCallbackD3D11, this);

      //SK_ReShade_InstallCopyResourceCallback          (SK_ReShade_OnCopyResourceCallbackD3D11,  this);
      //SK_ReShade_InstallSetDepthStencilViewCallback   (SK_ReShade_OnSetDepthStencilViewD3D11,   this);
      //SK_ReShade_InstallGetDepthStencilViewCallback   (SK_ReShade_OnGetDepthStencilViewD3D11,   this);
      //SK_ReShade_InstallClearDepthStencilViewCallback (SK_ReShade_OnClearDepthStencilViewD3D11, this);
      //SK_ReShade_InstallDrawCallback                  (SK_ReShade_OnDrawD3D11,                  this);

      first = false;
    }

		if ((! is_initialized ()) || _drawcalls.load () == 0)
		{
			return;
		}

#if 0
    if (is_effect_loaded ())
    {
      if (ReadPointerAcquire ((void **)&_techniques [0].timer.disjoint_query.async) == nullptr)
      {
        D3D11_QUERY_DESC query_desc {
          D3D11_QUERY_TIMESTAMP_DISJOINT, 0x00
        };
      
        ID3D11Query* pQuery = nullptr;
        if (SUCCEEDED (_device->CreateQuery (&query_desc, &pQuery)))
        {
          InterlockedExchangePointer ((void **)&_techniques [0].timer.disjoint_query.async, pQuery);
          _immediate_context->Begin (pQuery);
          InterlockedExchange ((volatile unsigned long *)&_techniques [0].timer.disjoint_query.active, TRUE);
        }
      }
    }
#endif


    if (explicit_draw.pass)
    {
      CComPtr <ID3D11Resource>         pRTVRes = nullptr;
      CComPtr <ID3D11RenderTargetView> pRTV    = nullptr;
            D3D11_TEXTURE2D_DESC tex_desc = { };

		  // Apply post processing
		  if (is_effect_loaded ())
		  {
        CComPtr <ID3D11DepthStencilView> pDSV = nullptr;
        CComPtr <ID3D11RenderTargetView> pRTVs [D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        _immediate_context->OMGetRenderTargets (D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &pRTVs [0], &pDSV);

        D3D11_TEXTURE2D_DESC                  bb_desc = { };
        _backbuffer_texture.get ()->GetDesc (&bb_desc);

        //pRTV = pRTVs [0];
        //    CComPtr   <ID3D11Resource >          pRTVRes = nullptr;
        //                     pRTV->GetResource (&pRTVRes);
        //    CComQIPtr <ID3D11Texture2D> pRTVTex (pRTVRes);
        //
        //    pRTVTex->GetDesc   (&tex_desc);
        for ( auto it : pRTVs )
        {
          if (it == nullptr)
            continue;
        
          D3D11_RENDER_TARGET_VIEW_DESC rt_desc =  { };
                          it->GetDesc (&rt_desc);

          if (/*rt_desc.Format == bb_desc.Format && */rt_desc.Texture2D.MipSlice == 0)
          {
            CComPtr   <ID3D11Resource >          pRTVRes = nullptr;
                               it->GetResource (&pRTVRes);
            CComQIPtr <ID3D11Texture2D> pRTVTex (pRTVRes);
        
            pRTVTex->GetDesc   (&tex_desc);
        
            if ( tex_desc.SampleDesc.Count == 1              &&
                 tex_desc.ArraySize        == 1              &&
                 tex_desc.MipLevels        <= 1              &&
                (tex_desc.BindFlags        & ( D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE ) ) )
            {
              pRTV = it;
              break;
            }
          }
        }

        if (pRTV != nullptr)
        {
          // Capture device state
          _stateblock.capture (_immediate_context.get ());
          
          // Disable unused pipeline stages
          _immediate_context->HSSetShader (nullptr, nullptr, 0);
          _immediate_context->DSSetShader (nullptr, nullptr, 0);
          _immediate_context->GSSetShader (nullptr, nullptr, 0);
          
          const uintptr_t null = 0;

          pRTV->GetResource (&pRTVRes);

          D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
          srv_desc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
          srv_desc.Format                          = tex_desc.Format;
          srv_desc.Texture2D.MipLevels             = 1;

          CComPtr <ID3D11ShaderResourceView> pSRV = nullptr;

          bool view = SUCCEEDED (_device->CreateShaderResourceView (pRTVRes, &srv_desc, &pSRV));

          const auto rtv = _backbuffer_rtv [0].get ();
          _immediate_context->OMSetRenderTargets (1, &rtv, nullptr);

          D3D11_DEPTH_STENCIL_DESC stencil_desc = { };
          stencil_desc.DepthEnable   = FALSE;
          stencil_desc.DepthFunc     = D3D11_COMPARISON_ALWAYS;
          stencil_desc.StencilEnable = FALSE;

          CComPtr <ID3D11DepthStencilState>                 pDepthState = nullptr;
          _device->CreateDepthStencilState (&stencil_desc, &pDepthState);

          _immediate_context->OMSetDepthStencilState (pDepthState, 0);
          _immediate_context->RSSetState             (_effect_rasterizer_state.get ());
        
          _immediate_context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
          _immediate_context->IASetInputLayout       (nullptr);
          _immediate_context->IASetVertexBuffers     (0, 1, reinterpret_cast<ID3D11Buffer *const *>(&null), reinterpret_cast<const UINT *>(&null), reinterpret_cast<const UINT *>(&null));

          _immediate_context->RSSetState  (_effect_rasterizer_state.get ());

          _immediate_context->VSSetShader (_copy_vertex_shader.get (), nullptr, 0);
          _immediate_context->PSSetShader (_copy_pixel_shader.get  (), nullptr, 0);

          //// Setup samplers
          _immediate_context->VSSetSamplers (0, static_cast <UINT> (_effect_sampler_states.size ()), _effect_sampler_states.data ());
          _immediate_context->PSSetSamplers (0, static_cast <UINT> (_effect_sampler_states.size ()), _effect_sampler_states.data ());

          const auto sst = _copy_sampler.get ();

          if (view)
          {
            _immediate_context->PSSetSamplers        (0, 1, &sst);
            _immediate_context->PSSetShaderResources (0, 1, &pSRV);
            
            _immediate_context->Draw                 (3, 0);
          }
          else
            _immediate_context->ResolveSubresource (_backbuffer_resolved.get (), 0, pRTVRes, 0, _backbuffer_format);
        
          _immediate_context->RSSetState (_effect_rasterizer_state.get ());
        
          {
            on_present_effect ();
          }
        
          _immediate_context->CopyResource       (_backbuffer_texture.get (), _backbuffer_resolved.get ());
          _immediate_context->OMSetRenderTargets (1, &pRTV, nullptr);
          
          
          _immediate_context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
          _immediate_context->IASetInputLayout       (nullptr);
          _immediate_context->IASetVertexBuffers     (0, 1, reinterpret_cast<ID3D11Buffer *const *>(&null), reinterpret_cast<const UINT *>(&null), reinterpret_cast<const UINT *>(&null));
          
          _immediate_context->RSSetState  (_effect_rasterizer_state.get ());
          
          _immediate_context->VSSetShader (_copy_vertex_shader.get (), nullptr, 0);
          _immediate_context->PSSetShader (_copy_pixel_shader.get  (), nullptr, 0);
          
          const auto srv = _backbuffer_texture_srv [make_format_srgb(_backbuffer_format) == _backbuffer_format].get();
          _immediate_context->PSSetSamplers        (0, 1, &sst);
          _immediate_context->PSSetShaderResources (0, 1, &srv);
        
          _immediate_context->Draw                 (3, 0);

          // Apply previous device state
          _stateblock.apply_and_release ();

          explicit_draw.calls++;
        }
      }
    }


    else
    {
      detect_depth_source ();

		  // Apply presenting
		  runtime::on_present ();

      if (last_calls == explicit_draw.calls)
      {
		    // Apply post processing
		    if (is_effect_loaded ())
		    {
		      // Capture device state
		      _stateblock.capture (_immediate_context.get ());

		      // Disable unused pipeline stages
		      _immediate_context->HSSetShader (nullptr, nullptr, 0);
		      _immediate_context->DSSetShader (nullptr, nullptr, 0);
		      _immediate_context->GSSetShader (nullptr, nullptr, 0);

		      // Setup real back buffer
		      const auto rtv = _backbuffer_rtv [0].get ();
		      _immediate_context->OMSetRenderTargets (1, &rtv, nullptr);

		      // Resolve back buffer
		      if (_backbuffer_resolved != _backbuffer)
		      {
		      	_immediate_context->ResolveSubresource(_backbuffer_resolved.get(), 0, _backbuffer.get(), 0, _backbuffer_format);
		      }

		      // Setup vertex input
		      const uintptr_t null = 0;
		      _immediate_context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		      _immediate_context->IASetInputLayout       (nullptr);
		      _immediate_context->IASetVertexBuffers     (0, 1, reinterpret_cast<ID3D11Buffer *const *>(&null), reinterpret_cast<const UINT *>(&null), reinterpret_cast<const UINT *>(&null));

		      _immediate_context->RSSetState (_effect_rasterizer_state.get ());

          D3D11_DEPTH_STENCIL_DESC stencil_desc = { };
          stencil_desc.DepthEnable   = FALSE;
          stencil_desc.DepthFunc     = D3D11_COMPARISON_ALWAYS;
          stencil_desc.StencilEnable = FALSE;

          CComPtr <ID3D11DepthStencilState> pDepthState = nullptr;
          _device->CreateDepthStencilState (&stencil_desc, &pDepthState);

		      // Setup samplers
		      _immediate_context->VSSetSamplers (0, static_cast <UINT> (_effect_sampler_states.size ()), _effect_sampler_states.data ());
		      _immediate_context->PSSetSamplers (0, static_cast <UINT> (_effect_sampler_states.size ()), _effect_sampler_states.data ());

		      on_present_effect ();

		      // Copy to back buffer
		      if (_backbuffer_resolved != _backbuffer)
		      {
		      	_immediate_context->CopyResource(_backbuffer_texture.get(), _backbuffer_resolved.get());

		      	const auto rtv = _backbuffer_rtv[2].get();
		      	_immediate_context->OMSetRenderTargets(1, &rtv, nullptr);

		      	const uintptr_t null = 0;
		      	_immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		      	_immediate_context->IASetInputLayout(nullptr);
		      	_immediate_context->IASetVertexBuffers(0, 1, reinterpret_cast<ID3D11Buffer *const *>(&null), reinterpret_cast<const UINT *>(&null), reinterpret_cast<const UINT *>(&null));

		      	_immediate_context->RSSetState(_effect_rasterizer_state.get());

		      	_immediate_context->VSSetShader(_copy_vertex_shader.get(), nullptr, 0);
		      	_immediate_context->PSSetShader(_copy_pixel_shader.get(), nullptr, 0);
		      	const auto sst = _copy_sampler.get();
		      	_immediate_context->PSSetSamplers(0, 1, &sst);
		      	const auto srv = _backbuffer_texture_srv[make_format_srgb(_backbuffer_format) == _backbuffer_format].get();
		      	_immediate_context->PSSetShaderResources(0, 1, &srv);

		      	_immediate_context->Draw(3, 0);
		      }
		      // Apply previous device state
		      _stateblock.apply_and_release ();
        }
      }

#if 0
      if (is_effect_loaded ())
      {
        if ((! _techniques [0].timer.disjoint_done) && ReadPointerAcquire ((volatile PVOID *)_techniques [0].timer.disjoint_query.async))
        {
          if (ReadAcquire ((volatile const LONG *)&_techniques [0].timer.disjoint_query.active))
          {
            _immediate_context->End ((ID3D11Asynchronous *)ReadPointerAcquire ((volatile PVOID*)&_techniques [0].timer.disjoint_query.async));
            InterlockedExchange ((volatile unsigned long *)&_techniques [0].timer.disjoint_query.active, FALSE);
          }

          else
          {
            HRESULT const hr =
                _immediate_context->GetData ( (ID3D11Asynchronous *)ReadPointerAcquire ((volatile PVOID*)&_techniques [0].timer.disjoint_query.async),
                                    &_techniques [0].timer.disjoint_query.last_results,
                                      sizeof D3D11_QUERY_DATA_TIMESTAMP_DISJOINT,
                                        0x0 );

            if (hr == S_OK)
            {
              ((ID3D11Asynchronous *)ReadPointerAcquire ((volatile PVOID*)&_techniques [0].timer.disjoint_query.async))->Release ();
              InterlockedExchangePointer ((void **)&_techniques [0].timer.disjoint_query.async, nullptr);


              // Check for failure, if so, toss out the results.
              if (! _techniques [0].timer.disjoint_query.last_results.Disjoint)
                _techniques [0].timer.disjoint_done = true;

              else
              {
                for (auto& technique : _techniques)
                {
                  technique.timer.timer.start.active = 0;
                  technique.timer.timer.end.active   = 0;
              
                  if (technique.timer.timer.start.async != nullptr)
                  {
                    SK_COM_ValidateRelease ((IUnknown **)&technique.timer.timer.start.async);
                                 technique.timer.timer.start.async = nullptr;
                  }
              
                  if (technique.timer.timer.end.async != nullptr)
                  {
                    SK_COM_ValidateRelease ((IUnknown **)&technique.timer.timer.end.async);
                                 technique.timer.timer.end.async = nullptr;
                  }
                }
                _techniques [0].timer.disjoint_done = true;
              }
            }
          }
        }

      if (_techniques [0].timer.disjoint_done)
      {
        for (auto& technique : _techniques)
      {
        auto GetTimerDataStart = [](ID3D11DeviceContext* dev_ctx, duration* pDuration, bool& success) ->
          UINT64
          {
            if (! FAILED (dev_ctx->GetData ( (ID3D11Query *)ReadPointerAcquire ((volatile PVOID *)&pDuration->start.async), &pDuration->start.last_results, sizeof UINT64, 0x00 )))
            {
              SK_COM_ValidateRelease ((IUnknown **)&pDuration->start.async);

              success = true;
              
              return pDuration->start.last_results;
            }

            success = false;

            return 0;
          };

        auto GetTimerDataEnd = [](ID3D11DeviceContext* dev_ctx, duration* pDuration, bool& success) ->
          UINT64
          {
            if (pDuration->end.async == nullptr)
            {
              success = true;
              return pDuration->start.last_results;
            }

            if (! FAILED (dev_ctx->GetData ( (ID3D11Query *)ReadPointerAcquire ((volatile PVOID *)&pDuration->end.async), &pDuration->end.last_results, sizeof UINT64, 0x00 )))
            {
              SK_COM_ValidateRelease ((IUnknown **)&pDuration->end.async);

              success = true;

              return pDuration->end.last_results;
            }

            success = false;

            return 0;
          };

        auto CalcRuntimeMS = [&](gpu_interval_timer* timer)
        {
          if (ReadAcquire64 ((volatile LONG64 *)&timer->runtime_ticks) != 0LL)
          {
            timer->runtime_ms =
              1000.0 * (((double)(ULONG64)ReadAcquire64 ((volatile LONG64 *)&timer->runtime_ticks)) / (double)_techniques [0].timer.disjoint_query.last_results.Frequency);

            // Filter out queries that spanned multiple frames
            //
            if (timer->runtime_ms > 0.0 && timer->last_runtime_ms > 0.0)
            {
              if (timer->runtime_ms > timer->last_runtime_ms * 100.0 || timer->runtime_ms > 12.0)
                timer->runtime_ms = timer->last_runtime_ms;
            }

            timer->last_runtime_ms = timer->runtime_ms;
          }
        };

        auto AccumulateRuntimeTicks = [&](ID3D11DeviceContext* dev_ctx, gpu_interval_timer* timer) ->
          void
          {
            std::vector <duration> rejects;

            InterlockedExchange64 ((volatile LONG64 *)&timer->runtime_ticks, 0LL);

            bool   success0 = false, success1 = false;
            UINT64 time0    = 0ULL,  time1    = 0ULL;

            time0 = GetTimerDataEnd   (dev_ctx, &timer->timer, success0);
            time1 = GetTimerDataStart (dev_ctx, &timer->timer, success1);

            if (success0 && success1)
              InterlockedAdd64 ((volatile LONG64 *)&timer->runtime_ticks, time0 - time1);
            else
              rejects.push_back (timer->timer);

            // If effect was cancelled ...
            //{
            //  InterlockedExchange64 ((volatile LONG64 *)&tracker->runtime_ticks, 0LL);
            //  timer->runtime_ms      = 0.0;
            //  timer->last_runtime_ms = 0.0;
            //}

            // Anything that fails goes back on the list and we will try again next frame
            //if (! rejects.empty ())
            //  timer->timer = rejects [0];
          };


          AccumulateRuntimeTicks (_immediate_context.get (), &technique.timer);
          CalcRuntimeMS          (&technique.timer);

          technique.average_gpu_duration.append (technique.timer.last_runtime_ms);
        }
        _techniques [0].timer.disjoint_done = false;
      }
      }
#endif

      last_calls = explicit_draw.calls;
    }
	}

	void
	d3d11_runtime::on_draw_call (ID3D11DeviceContext *context, unsigned int vertices)
	{
		_vertices  += vertices;
		_drawcalls += 1;

		com_ptr <ID3D11DepthStencilView> current_depthstencil = nullptr;

		context->OMGetRenderTargets ( 0, nullptr,
                                    &current_depthstencil );

		if ( current_depthstencil == nullptr ||
			   current_depthstencil == _default_depthstencil )
		{
			return;
		}

		if (current_depthstencil == _depthstencil_replacement)
		{
			current_depthstencil = _depthstencil;
		}

		const auto it =
			_depth_source_table.find (current_depthstencil.get ());

		if (it != _depth_source_table.cend () && (! it->second.invalidated))
		{
			it->second.drawcall_count = _drawcalls.load ();
			it->second.vertices_count += vertices;
		}
	}

	void
	d3d11_runtime::on_set_depthstencil_view (ID3D11DepthStencilView *&depthstencil)
	{
		if ( (! _depth_source_table.count (depthstencil)) || _depth_source_table [depthstencil].invalidated )
		{
			// Replace instead of insert
			bool invalidated = (_depth_source_table.count (depthstencil));

			D3D11_TEXTURE2D_DESC texture_desc = { };

			com_ptr <ID3D11Resource>  resource = nullptr;
			com_ptr <ID3D11Texture2D> texture  = nullptr;

			depthstencil->GetResource (&resource);

			if (FAILED (resource->QueryInterface (&texture)))
			{
				return;
			}

			texture->GetDesc (&texture_desc);

			// Early depth stencil rejection
			if ( texture_desc.Width            != _width  ||
			     texture_desc.Height           != _height ||
			     texture_desc.SampleDesc.Count >  1 )
			{
				return;
			}

			depthstencil->AddRef ();

			// Begin tracking new depth stencil
			const depth_source_info info =
				{ texture_desc.Width, texture_desc.Height,
				  0,                  0,
				  false
				};

			_depth_source_table [depthstencil] = info;
		}

		if (_depthstencil_replacement != nullptr && depthstencil == _depthstencil)
		{
			depthstencil = _depthstencil_replacement.get ();
		}
	}

	void
	d3d11_runtime::on_get_depthstencil_view (ID3D11DepthStencilView *&depthstencil)
	{
		if ( _depthstencil_replacement != nullptr                &&
		                  depthstencil == _depthstencil_replacement )
		{
			depthstencil->Release ();

			depthstencil =
				_depthstencil.get   ();

			depthstencil->AddRef  ();
		}
	}

	void
	d3d11_runtime::on_clear_depthstencil_view (ID3D11DepthStencilView *&depthstencil)
	{
		if ( _depthstencil_replacement != nullptr    &&
		                  depthstencil == _depthstencil )
		{
			depthstencil =
				_depthstencil_replacement.get ();
		}
	}

	void
	d3d11_runtime::on_copy_resource (ID3D11Resource *&dest, ID3D11Resource *&source)
	{
		if (_depthstencil_replacement != nullptr)
		{
			com_ptr <ID3D11Resource> resource = nullptr;

			_depthstencil->GetResource (&resource);

			if (dest == resource)
			{
				dest =
					_depthstencil_texture.get ();
			}
			if (source == resource)
			{
				source =
					_depthstencil_texture.get ();
			}
		}
	}

	void
	d3d11_runtime::capture_frame(uint8_t *buffer) const
	{
		if (_backbuffer_format != DXGI_FORMAT_R8G8B8A8_UNORM      &&
			  _backbuffer_format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
			  _backbuffer_format != DXGI_FORMAT_B8G8R8A8_UNORM      &&
			  _backbuffer_format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
		{
			LOG (WARNING) << "Screenshots are not supported for back buffer format " << _backbuffer_format << ".";
			return;
		}

		D3D11_TEXTURE2D_DESC texture_desc = { };

		texture_desc.Width                = _width;
		texture_desc.Height               = _height;
		texture_desc.ArraySize            = 1;
		texture_desc.MipLevels            = 1;
		texture_desc.Format               = _backbuffer_format;
		texture_desc.SampleDesc.Count     = 1;
		texture_desc.Usage                = D3D11_USAGE_STAGING;
		texture_desc.CPUAccessFlags       = D3D11_CPU_ACCESS_READ;

		com_ptr<ID3D11Texture2D> texture_staging;

		HRESULT hr =
			_device->CreateTexture2D (&texture_desc, nullptr, &texture_staging);

		if (FAILED (hr))
		{
			LOG (ERROR) << "Failed to create staging resource for screenshot capture! HRESULT is '" << std::hex << hr << std::dec << "'.";
			return;
		}

		_immediate_context->CopyResource ( texture_staging.get        (),
		                                     _backbuffer_resolved.get () );

		D3D11_MAPPED_SUBRESOURCE mapped = { };

		hr =
			_immediate_context->Map (texture_staging.get (), 0, D3D11_MAP_READ, 0, &mapped);

		if (FAILED (hr))
		{
			LOG(ERROR) << "Failed to map staging resource with screenshot capture! HRESULT is '" << std::hex << hr << std::dec << "'.";
			return;
		}

		      auto mapped_data = static_cast <BYTE *> (mapped.pData);
		const UINT pitch       = texture_desc.Width * 4;

		for (UINT y = 0; y < texture_desc.Height; y++)
		{
			CopyMemory (buffer, mapped_data, std::min (pitch, static_cast <UINT> (mapped.RowPitch)));

			for (UINT x = 0; x < pitch; x += 4)
			{
				buffer [x + 3] = 0xFF;

				if ( texture_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
				     texture_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB )
				{
					std::swap (buffer [x + 0], buffer [x + 2]);
				}
			}

			buffer      += pitch;
			mapped_data += mapped.RowPitch;
		}

		_immediate_context->Unmap (texture_staging.get (), 0);
	}

	bool
	d3d11_runtime::load_effect (const reshadefx::syntax_tree &ast, std::string &errors)
	{
		return d3d11_effect_compiler (this, ast, errors, false).run ();
	}

	bool
	d3d11_runtime::update_texture (texture &texture, const uint8_t *data)
	{
		if (texture.impl_reference != texture_reference::none)
		{
			return false;
		}

		const auto texture_impl =
		             texture.impl->as <d3d11_tex_data> ();

		assert (data         != nullptr);
		assert (texture_impl != nullptr);

		switch (texture.format)
		{
			case texture_format::r8:
			{
				std::vector <uint8_t> data2 (texture.width * texture.height);

				for (size_t i = 0, k = 0; i < texture.width * texture.height * 4; i += 4, k++)
					data2 [k] = data [i];

				_immediate_context->UpdateSubresource ( texture_impl->texture.get (),
				                                          0, nullptr, data2.data  (),
				                                            texture.width, texture.width * texture.height );
			} break;

			case texture_format::rg8:
			{
				std::vector <uint8_t> data2 (texture.width * texture.height * 2);

				for (size_t i = 0, k = 0; i < texture.width * texture.height * 4; i += 4, k += 2)
					data2 [k    ] = data [i    ],
					data2 [k + 1] = data [i + 1];

				_immediate_context->UpdateSubresource ( texture_impl->texture.get (),
				                                          0, nullptr, data2.data  (),
				                                            texture.width * 2, texture.width * texture.height * 2 );
			} break;

			default:
			{
				_immediate_context->UpdateSubresource ( texture_impl->texture.get (),
				                                          0, nullptr, data,
				                                            texture.width * 4, texture.width * texture.height * 4 );
			} break;
		}

		if (texture.levels > 1)
		{
			_immediate_context->GenerateMips (texture_impl->srv [0].get ());
		}

		return true;
	}

	void
	d3d11_runtime::render_technique (const technique &technique)
	{ 
#if 0   
    if (_techniques [0].timer.disjoint_query.active)
    {
      // Start a new query
      D3D11_QUERY_DESC query_desc {
        D3D11_QUERY_TIMESTAMP, 0x00
      };
    
      duration duration_;
    
      ID3D11Query* pQuery = nullptr;
      if (SUCCEEDED (_device->CreateQuery (&query_desc, &pQuery)))
      {
        InterlockedExchangePointer ((void **)&technique.timer.timer.start.async, pQuery);
        _immediate_context->End     (pQuery);
      }
    }
#endif



		bool is_default_depthstencil_cleared = false;

		// Setup shader constants
		if (technique.uniform_storage_index >= 0)
		{
			const auto constant_buffer =
				_constant_buffers [technique.uniform_storage_index].get ();

			D3D11_MAPPED_SUBRESOURCE mapped = { };

			const HRESULT hr =
				_immediate_context->Map (constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

			if (SUCCEEDED (hr))
			{
				CopyMemory (mapped.pData, get_uniform_value_storage().data() + technique.uniform_storage_offset, mapped.RowPitch);

				_immediate_context->Unmap (constant_buffer, 0);
			}
			else
			{
				LOG(ERROR) << "Failed to map constant buffer! HRESULT is '" << std::hex << hr << std::dec << "'!";
			}

			_immediate_context->VSSetConstantBuffers (0, 1, &constant_buffer);
			_immediate_context->PSSetConstantBuffers (0, 1, &constant_buffer);
		}

		for (const auto &pass_object : technique.passes)
		{
			const d3d11_pass_data &pass =
			  *pass_object->as <d3d11_pass_data> ();

			// Setup states
			_immediate_context->VSSetShader (pass.vertex_shader.get (), nullptr, 0);
			_immediate_context->PSSetShader (pass.pixel_shader.get  (), nullptr, 0);

      //if (pass.shader_resources.empty ())
      //  continue;

			static const float blendfactor [4] = { 1.0f, 1.0f, 1.0f, 1.0f };

			_immediate_context->OMSetBlendState        (pass.blend_state.get         (), blendfactor, D3D11_DEFAULT_SAMPLE_MASK);
			_immediate_context->OMSetDepthStencilState (pass.depth_stencil_state.get (), pass.stencil_reference);

			// Save back buffer of previous pass
			_immediate_context->CopyResource ( _backbuffer_texture.get    (),
			                                     _backbuffer_resolved.get () );

			// Setup shader resources
			_immediate_context->VSSetShaderResources (0, static_cast <UINT> (pass.shader_resources.size ()), pass.shader_resources.data ());
			_immediate_context->PSSetShaderResources (0, static_cast <UINT> (pass.shader_resources.size ()), pass.shader_resources.data ());

			// Setup render targets
			if ( static_cast <UINT> (pass.viewport.Width)  == _width &&
			     static_cast <UINT> (pass.viewport.Height) == _height )
			{
				_immediate_context->OMSetRenderTargets (D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, pass.render_targets, _default_depthstencil.get());

				if (!is_default_depthstencil_cleared)
				{
					is_default_depthstencil_cleared = true;

					_immediate_context->ClearDepthStencilView (_default_depthstencil.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
				}
			}
			else
			{
				_immediate_context->OMSetRenderTargets (D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, pass.render_targets, nullptr);
			}

			_immediate_context->RSSetViewports (1, &pass.viewport);

			if (pass.clear_render_targets)
			{
				for (const auto target : pass.render_targets)
				{
					if (target != nullptr)
					{
						constexpr float color [4] = { 0.0f, 0.0f, 0.0f, 0.0f };
						_immediate_context->ClearRenderTargetView(target, color);
					}
				}
			}

			// Draw triangle
			_immediate_context->Draw (3, 0);

			_vertices  += 3;
			_drawcalls += 1;

			// Reset render targets
			_immediate_context->OMSetRenderTargets ( 0, nullptr, nullptr );

			// Reset shader resources
			ID3D11ShaderResourceView* null [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };

			_immediate_context->VSSetShaderResources (0, static_cast <UINT> (pass.shader_resources.size ()), null);
			_immediate_context->PSSetShaderResources (0, static_cast <UINT> (pass.shader_resources.size ()), null);

			// Update shader resources
			for (const auto resource : pass.render_target_resources)
			{
				if (resource == nullptr)
				{
					continue;
				}

				D3D11_SHADER_RESOURCE_VIEW_DESC resource_desc = { };

				resource->GetDesc (&resource_desc);

				if (resource_desc.Texture2D.MipLevels > 1)
				{
					_immediate_context->GenerateMips (resource);
				}
			}
		}


#if 0
    if (_techniques [0].timer.disjoint_query.active)
    {
      D3D11_QUERY_DESC query_desc {
        D3D11_QUERY_TIMESTAMP, 0x00
      };
    
                                            ID3D11Query* pQuery = nullptr;
      if (SUCCEEDED (_device->CreateQuery (&query_desc, &pQuery)))
      {
        InterlockedExchangePointer ((void **)&technique.timer.timer.end.async, pQuery);
                                                      _immediate_context->End (pQuery);
      }
    }
#endif
	}

	void
	d3d11_runtime::render_imgui_draw_data (ImDrawData *draw_data)
	{
    ImGui_ImplDX11_RenderDrawLists (draw_data);
	}

	void
	d3d11_runtime::detect_depth_source (void)
	{
		if ( _is_multisampling_enabled || _depth_source_table.empty () )
		{
			return;
		}

		depth_source_info       best_info  = { 0 };
		ID3D11DepthStencilView *best_match = nullptr;

		for (auto it = _depth_source_table.begin (); it != _depth_source_table.end ();)
		{
			const auto  depthstencil      = it->first;
			      auto &depthstencil_info = it->second;

			if ((depthstencil->AddRef (), depthstencil->Release ()) == 1)
			{
				depthstencil->Release ();

				depthstencil_info.invalidated = TRUE;
				++it;

				continue;
			}

			else
			{
				++it;
			}

			if (depthstencil_info.drawcall_count == 0)
			{
				continue;
			}

			if ((depthstencil_info.vertices_count * (1.2f - float(depthstencil_info.drawcall_count) / _drawcalls.load ())) >= (best_info.vertices_count * (1.2f - float(best_info.drawcall_count) / _drawcalls.load ())))
			{
				best_match = depthstencil;
				best_info  = depthstencil_info;
			}

			depthstencil_info.drawcall_count = depthstencil_info.vertices_count = 0;
		}

    static int iters = 0;

    if (! (++iters % 1000))
    {
		  std::map <ID3D11DepthStencilView *, depth_source_info> live_views;

		  // Trim the table
		  for (auto it : _depth_source_table)
		  {
		  	if (! it.second.invalidated)
		  	{
		  		live_views.emplace (it.first, it.second);
		  	}
		  }
      
		  _depth_source_table.clear ();
      
		  for (auto it : live_views)
		  {
		  	_depth_source_table.insert (std::make_pair (it.first, it.second));
		  }
		}

		if (best_match != nullptr && _depthstencil != best_match)
		{
			create_depthstencil_replacement (best_match);
		}
	}

	bool
	d3d11_runtime::create_depthstencil_replacement (ID3D11DepthStencilView *depthstencil)
	{
		_depthstencil.reset             ();
		_depthstencil_replacement.reset ();
		_depthstencil_texture.reset     ();
		_depthstencil_texture_srv.reset ();

		if (depthstencil != nullptr)
		{
			_depthstencil = depthstencil;

			depthstencil->GetResource (reinterpret_cast <ID3D11Resource **> (&_depthstencil_texture));
		
			D3D11_TEXTURE2D_DESC texdesc = { };

			_depthstencil_texture->GetDesc (&texdesc);

			HRESULT hr = S_OK;

			if ((texdesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0)
			{
				_depthstencil_texture.reset ();

				switch (texdesc.Format)
				{
					case DXGI_FORMAT_R16_TYPELESS:
          case DXGI_FORMAT_R16_FLOAT:
					case DXGI_FORMAT_D16_UNORM:
						texdesc.Format = DXGI_FORMAT_R16_TYPELESS;
						break;

					case DXGI_FORMAT_R8_UNORM:
					case DXGI_FORMAT_R32_TYPELESS:
					case DXGI_FORMAT_R32_FLOAT:
					case DXGI_FORMAT_D32_FLOAT:
						texdesc.Format = DXGI_FORMAT_R32_TYPELESS;
						break;

					default:
					case DXGI_FORMAT_R24G8_TYPELESS:
					case DXGI_FORMAT_D24_UNORM_S8_UINT:
						texdesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
						break;

					case DXGI_FORMAT_R32G8X24_TYPELESS:
					case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
						texdesc.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
						break;
				}

				texdesc.BindFlags = D3D11_BIND_DEPTH_STENCIL |
				                    D3D11_BIND_SHADER_RESOURCE;

				hr =
					_device->CreateTexture2D (&texdesc, nullptr, &_depthstencil_texture);

				if (SUCCEEDED (hr))
				{
					D3D11_DEPTH_STENCIL_VIEW_DESC dsvdesc = { };

					dsvdesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

					switch (texdesc.Format)
					{
						case DXGI_FORMAT_R16_TYPELESS:
							dsvdesc.Format = DXGI_FORMAT_D16_UNORM;
							break;
						case DXGI_FORMAT_R32_TYPELESS:
							dsvdesc.Format = DXGI_FORMAT_D32_FLOAT;
							break;
						case DXGI_FORMAT_R24G8_TYPELESS:
							dsvdesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
							break;
						case DXGI_FORMAT_R32G8X24_TYPELESS:
							dsvdesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
							break;
					}

					hr =
						_device->CreateDepthStencilView (_depthstencil_texture.get (), &dsvdesc, &_depthstencil_replacement);
				}
			}

			if (FAILED (hr))
			{
				LOG(ERROR) << "Failed to create depth stencil replacement texture! HRESULT is '" << std::hex << hr << std::dec << "'.";

				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvdesc = { };

			srvdesc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvdesc.Texture2D.MipLevels = 1;

			switch (texdesc.Format)
			{
				case DXGI_FORMAT_R16_TYPELESS:
					srvdesc.Format = DXGI_FORMAT_R16_FLOAT;
					break;
				case DXGI_FORMAT_R32_TYPELESS:
					srvdesc.Format = DXGI_FORMAT_R32_FLOAT;
					break;
				case DXGI_FORMAT_R24G8_TYPELESS:
					srvdesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
					break;
				case DXGI_FORMAT_R32G8X24_TYPELESS:
					srvdesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
					break;
			}

			hr =
				_device->CreateShaderResourceView (_depthstencil_texture.get (), &srvdesc, &_depthstencil_texture_srv);

			if (FAILED (hr))
			{
				LOG (ERROR) << "Failed to create depth stencil replacement resource view! HRESULT is '" << std::hex << hr << std::dec << "'.";

				return false;
			}

			if (_depthstencil != _depthstencil_replacement)
			{
				// Update auto depth stencil
				com_ptr <ID3D11DepthStencilView> current_depthstencil = nullptr;

				ID3D11RenderTargetView *targets [D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { nullptr };

				_immediate_context->OMGetRenderTargets (D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &current_depthstencil);

				if (current_depthstencil != nullptr && current_depthstencil == _depthstencil)
				{
					_immediate_context->OMSetRenderTargets (D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, _depthstencil_replacement.get ());
				}

				for (auto& target : targets)
				{
					if (target != nullptr)
					{
						target->Release ();
					}
				}
			}
		}

		// Update effect textures
		_effect_shader_resources [2] = _depthstencil_texture_srv.get ();

		for (const auto   &technique : _techniques)
			for (const auto &pass      :  technique.passes)
				pass->as <d3d11_pass_data> ()->shader_resources [2] = _depthstencil_texture_srv.get ();

		return true;
	}
}