/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2015 TOSHIBA Corporation.
 * Copyright (C) 2017 Toshiba Electronic Devices & Storage Corporation.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*************************************************************************//**
 * @file gsttsbecnrrx.c
 * @brief GStreamer ECNR RXプラグイン
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include <gst/gst.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gsttsbecnrrx.h"

typedef unsigned long snd_pcm_uframes_t;	/**< ダミー定義：alsa-lib snd_pcm_writei()引数で使用 */
typedef long snd_pcm_sframes_t;				/**< ダミー定義：alsa-lib snd_pcm_readi()引数で使用 */
#include "pcm_tsb_ecnr.h"

GST_DEBUG_CATEGORY_STATIC( gst_tsbecnrrx_debug );

#define GST_CAT_DEFAULT gst_tsbecnrrx_debug


/* Filter signals and args */
enum
{
	RX_RESET_REQUEST_SIGNAL,	/**< RX処理初期化要求シグナル */
#ifdef TSB_ECNR_GET_RX_DATA_REQUEST
	GET_DATA_REQUEST_SIGNAL,	/**< RX参照信号取得要求シグナル */
#endif  /* TSB_ECNR_GET_RX_DATA_REQUEST */
	LAST_SIGNAL
};


enum
{
	PROP_0,
	PROP_TSB_PARAM,			/**< TSBパラメータ設定プロパティ */
	PROP_RX_PARAM,			/**< RXパラメータ設定プロパティ */
	PROP_RX_ECNR_PARAM		/**< ECNR RXエレメント 初期化・終了プロパティ */
};


static guint tsb_ecnr_signals[ LAST_SIGNAL ] = { 0 };  /**< RX参照信号取得要求シグナル配列 */


/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
#ifndef GST_VER_1_0
	static GstStaticPadTemplate sink_factory =  /**< GStreamer0.10用SINKパッド ケイパビリティ */
				GST_STATIC_PAD_TEMPLATE
				(
					"sink",
					GST_PAD_SINK,
					GST_PAD_ALWAYS,
					GST_STATIC_CAPS
					(
						"audio/x-raw-int"
					)
				);
#else  /* GST_VER_1_0 */
	static GstStaticPadTemplate sink_factory =  /**< GStreamer1.0用SINKパッド ケイパビリティ */
				GST_STATIC_PAD_TEMPLATE
				(
					"sink",
					GST_PAD_SINK,
					GST_PAD_ALWAYS,
					GST_STATIC_CAPS
					(
						"audio/x-raw"
					)
				);
#endif  /* GST_VER_1_0 */

#ifndef GST_VER_1_0
	static GstStaticPadTemplate src_factory =  /**< GStreamer0.10用SRCパッド ケイパビリティ */
				GST_STATIC_PAD_TEMPLATE
				(
					"src",
					GST_PAD_SRC,
					GST_PAD_ALWAYS,
					GST_STATIC_CAPS
					(
						"audio/x-raw-int"
					)
				);
#else  /* GST_VER_1_0 */
	static GstStaticPadTemplate src_factory =  /**< GStreamer1.0用SRCパッド ケイパビリティ */
				GST_STATIC_PAD_TEMPLATE
				(
					"src",
					GST_PAD_SRC,
					GST_PAD_ALWAYS,
					GST_STATIC_CAPS
					(
						"audio/x-raw"
					)
				);
#endif  /* GST_VER_1_0 */

#define gst_tsbecnrrx_parent_class parent_class
G_DEFINE_TYPE( Gsttsbecnrrx, gst_tsbecnrrx, GST_TYPE_ELEMENT );

static void gst_tsbecnrrx_set_property( GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec );
static void gst_tsbecnrrx_get_property( GObject *object, guint prop_id, GValue *value, GParamSpec *pspec );

static GstStateChangeReturn gst_tsbecnrrx_change_state( GstElement *element, GstStateChange transition );

#ifndef GST_VER_1_0
	static GstFlowReturn gst_tsbecnrrx_chain( GstPad *pad, GstBuffer *buf );
#else  /* GST_VER_1_0 */
	static GstFlowReturn gst_tsbecnrrx_chain( GstPad *pad, GstObject *parent, GstBuffer *buf );
#endif  /* GST_VER_1_0 */

#ifdef GST_VER_1_0
	static gboolean gst_tsbecnrrx_sink_event( GstPad *pad, GstObject *parent, GstEvent *event );
#endif  /* GST_VER_1_0 */
static void gst_tsbecnrrx_set_timestamp( GstBuffer *inbuf, GstBuffer *outbuf );


/* GObject vmethod implementations */

/* initialize the tsbecnrrx's class */
/*************************************************************************//**
 * @brief ECNR RXエレメント 初期化処理
 * @param[in] klass
 * @par 説明
 *        ECNR RXエレメントの初期化を行う。<br>
 *        - set/getプロパティ登録
 *        - シグナル登録
 *        - ケイパビリティ登録
 *****************************************************************************/
static void gst_tsbecnrrx_class_init( GsttsbecnrrxClass *klass )
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
	
	gobject_class = (GObjectClass*)klass;
	gstelement_class = (GstElementClass*)klass;
	
	gobject_class->set_property = gst_tsbecnrrx_set_property;
	gobject_class->get_property = gst_tsbecnrrx_get_property;
	
	g_object_class_install_property(	gobject_class,
										PROP_TSB_PARAM,
										g_param_spec_pointer(	"TSB_PARAM",
																"TSB PARAM",
																"TSB parameter",
																G_PARAM_READWRITE ) );
	
	g_object_class_install_property(	gobject_class,
										PROP_RX_PARAM,
										g_param_spec_pointer(	"RX_PARAM",
																"RX PARAM",
																"RX parameter",
																G_PARAM_READWRITE ) );
	
	g_object_class_install_property(	gobject_class,
										PROP_RX_ECNR_PARAM,
										g_param_spec_int(	"RX_ECNR_PARAM",
															"RX ECNR PARAM",
															"RX ecnr param",
															G_MININT,
															G_MAXINT,
															0,
															G_PARAM_READWRITE ) );
	
	tsb_ecnr_signals[ RX_RESET_REQUEST_SIGNAL ] = g_signal_new(	"ECNR_RX_RESET_REQUEST",
																G_TYPE_FROM_CLASS( klass ),
																G_SIGNAL_RUN_LAST,
																0,
																0,
																NULL,
																g_cclosure_marshal_VOID__VOID,
																G_TYPE_NONE,
																0 );
	
#ifdef TSB_ECNR_GET_RX_DATA_REQUEST
	tsb_ecnr_signals[ GET_DATA_REQUEST_SIGNAL ] = g_signal_new(	"ECNR_RX_GET_DATA_REQUEST",
																G_TYPE_FROM_CLASS( klass ),
																G_SIGNAL_RUN_LAST,
																0,
																0,
																NULL,
	#ifndef GST_VER_1_0
																gst_marshal_BOOLEAN__POINTER,
	#else  /* GST_VER_1_0 */
																g_cclosure_marshal_VOID__POINTER,
	#endif  /* GST_VER_1_0 */
																G_TYPE_BOOLEAN,
																1,
																GST_TYPE_BUFFER );
#endif  /* TSB_ECNR_GET_RX_DATA_REQUEST */
	
	gst_element_class_set_details_simple(	gstelement_class,
											"Toshiba ECNR RX Proc",
											"Filter/Convert/Audio",
											"EchoCanceller & NoiseReduction",
											"TOSHIBA Corporation." );
	
	gst_element_class_add_pad_template( gstelement_class, gst_static_pad_template_get( &src_factory ) );
	gst_element_class_add_pad_template( gstelement_class, gst_static_pad_template_get( &sink_factory ) );
	
	gstelement_class->change_state = GST_DEBUG_FUNCPTR( gst_tsbecnrrx_change_state );
	
	return;
}


#ifndef GST_VER_1_0
/*************************************************************************//**
 * @brief ECNR RXエレメント ケイパビリティ受信処理
 * @param[in] pad
 * @param[in] caps
 * @par 説明
 *        GStreamer0.10専用処理。<br>
 *        ケイパビリティを受信した場合に本関数がコールされる。<br>
 *        ケイパビリティの中からサンプリング周波数、チャンネル数を取得し、<br>
 *        ECNR RXエレメント構造体に保存する。
 *****************************************************************************/
static gboolean gst_tsbecnrrx_setcaps( GstPad *pad, GstCaps *caps )
{
	Gsttsbecnrrx *filter;
	GstStructure *structure;
	int rate;
	int channels;
	gboolean format = FALSE;
	const gchar *format_str;
	gboolean ret;
	GstCaps *outcaps;
	
	filter = GST_TSBECNRRX( GST_PAD_PARENT( pad ) );
	
	structure = gst_caps_get_structure( caps, 0 );
	
	ret = gst_structure_get_int( structure, "rate", &rate );
	ret &= gst_structure_get_int( structure, "channels", &channels );
	
	if ( !ret )
	{
		return ( FALSE );
	}
	else
	{
		format_str = gst_structure_get_string( structure, "format" );
		
		if ( format_str )
		{
			if ( g_str_equal( format_str, "S16LE" ) )
			{
				format = TRUE;
			}
		}
		
		if ( ( rate == 24000 || rate == 16000 || rate == 8000 ) && ( channels == 1 ) && ( format == TRUE ) )
		{
			filter->rate = rate;
			filter->channels = channels;
			
			if ( ( filter->rate == 8000 ) && ( filter->nb_mode == 2 ) )
			{
				filter->nb_mode_caps_set_flag = TRUE;
				rate = 16000;
			}
		}
		else
		{
			filter->rate = 0;
			filter->channels = 0;
			filter->ecnr_switch = FALSE;
		}
	}
	
	outcaps = gst_caps_new_simple(	"audio/x-raw-int",
									"width",		G_TYPE_INT,		16,
									"depth",		G_TYPE_INT,		16,
									"endianness",	G_TYPE_INT,		G_BYTE_ORDER,
									"signed",		G_TYPE_BOOLEAN,	TRUE,
									"rate",			G_TYPE_INT,		rate,
									"channels",		G_TYPE_INT,		channels,
									NULL );
	
	ret = gst_pad_set_caps( filter->srcpad, outcaps );
	
	#ifdef TSB_ECNR_TWO_SRC
	ret &= gst_pad_set_caps( filter->rx_srcpad, outcaps );
	#endif  /* TSB_ECNR_TWO_SRC */
	
	gst_caps_unref( outcaps );
	
	return ( ret );
}
#endif  /* GST_VER_1_0 */


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
/*************************************************************************//**
 * @brief ECNR RXエレメント パッド初期化処理
 * @param[in] filter ECNR RXエレメント構造体
 * @par 説明
 *        ECNR RXエレメント構造体、SINK/SRCパッドの初期化を行う。
 *****************************************************************************/
static void gst_tsbecnrrx_init( Gsttsbecnrrx *filter )
{
	filter->sinkpad = gst_pad_new_from_static_template( &sink_factory, "sink" );
	
#ifndef GST_VER_1_0
	gst_pad_set_setcaps_function( filter->sinkpad, GST_DEBUG_FUNCPTR( gst_tsbecnrrx_setcaps ) );
#endif  /* GST_VER_1_0 */
	
	gst_pad_set_chain_function( filter->sinkpad, GST_DEBUG_FUNCPTR( gst_tsbecnrrx_chain ) );
	
#ifdef GST_VER_1_0
	gst_pad_set_event_function( filter->sinkpad, GST_DEBUG_FUNCPTR( gst_tsbecnrrx_sink_event ) );
#endif  /* GST_VER_1_0 */
	
#ifdef GST_VER_1_0
	GST_PAD_SET_PROXY_CAPS( filter->sinkpad );
#endif  /* GST_VER_1_0 */
	
	gst_element_add_pad( GST_ELEMENT( filter ), filter->sinkpad );
	
	
	filter->srcpad = gst_pad_new_from_static_template( &src_factory, "src" );
	
#ifdef GST_VER_1_0
	GST_PAD_SET_PROXY_CAPS( filter->srcpad );
#endif  /* GST_VER_1_0 */
	
	gst_element_add_pad( GST_ELEMENT( filter ), filter->srcpad );
	
#ifdef TSB_ECNR_TWO_SRC
	filter->rx_srcpad = gst_pad_new_from_static_template( &src_factory, "ecnrsrc" );
	gst_element_add_pad( GST_ELEMENT( filter ), filter->rx_srcpad );
#endif  /* TSB_ECNR_TWO_SRC */
	
	filter->init_flag = FALSE;
	filter->ecnr_switch = FALSE;
	filter->rate = 0;
	filter->channels = 0;
	filter->nb_mode = 0;
	filter->nb_mode_caps_set_flag = FALSE;
	
	return;
}


/*************************************************************************//**
 * @brief ECNR RXエレメント setプロパティ処理
 * @param[in] object
 * @param[in] prop_id
 * @param[in] value
 * @param[in] pspec
 * @par 説明
 *        上位からの要求によりプロパティを設定する。<br>
 *        - TSB_PARAMプロパティ：      snd_pcm_set__tsb_ecnr_TSB_parameter()をコールし、TSBパラメータを設定する。
 *        - RX_PARAMプロパティ：       snd_pcm_set__tsb_ecnr_RX_parameter()をコールし、RXパラメータを設定する。
 *        - RX_ECNR_PARAMプロパティ：  設定値が'1'の場合にはECNR初期化を行い、'0'の場合にはECNR終了処理を行う。
 *****************************************************************************/
static void gst_tsbecnrrx_set_property( GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec )
{
	Gsttsbecnrrx *filter = GST_TSBECNRRX( object );
	
	switch ( prop_id )
	{
		case PROP_TSB_PARAM:
			snd_pcm_set__tsb_ecnr_TSB_parameter( g_value_get_pointer( value ) );
			break;
		
		case PROP_RX_PARAM:
			snd_pcm_set__tsb_ecnr_RX_parameter( g_value_get_pointer( value ) );
			break;
		
		case PROP_RX_ECNR_PARAM:
			if ( g_value_get_int( value ) )
			{
				TSBECNR_RX_PARAM_t *rx_param = NULL;
				
				filter->init_flag = TRUE;
				filter->ecnr_switch = TRUE;
				
#ifndef VCPP
				snd_pcm_set__rx_data_transfer_method( 0 );
#else  /* VCPP */
				snd_pcm_set__rx_data_transfer_method( 1 );
#endif  /* VCPP */
				
				snd_pcm_set__tsb_ecnr_RX_ECNR_parameter( 1 );
				
				rx_param = (TSBECNR_RX_PARAM_t*)snd_pcm_get__tsb_ecnr_alsasink_parameter();
				
				if ( rx_param )
				{
					if ( ( rx_param->RX_SRC_IN_FS == 8000 ) && ( rx_param->RX_SRC_OUT_FS == 16000 ) )
					{
						filter->nb_mode_caps_set_flag = FALSE;
						filter->nb_mode = 2;
					}
					else
					{
						filter->nb_mode = 1;
					}
				}
			}
			else
			{
				filter->init_flag = FALSE;
				filter->ecnr_switch = FALSE;
				filter->rate = 0;
				filter->channels = 0;
				filter->nb_mode = 0;
				filter->nb_mode_caps_set_flag = FALSE;
				
#ifndef VCPP
				snd_pcm_set__rx_data_transfer_method( 0 );
#else  /* VCPP */
				snd_pcm_set__rx_data_transfer_method( 1 );
#endif  /* VCPP */
				
				snd_pcm_set__tsb_ecnr_RX_ECNR_parameter( 0 );
			}
			break;
		
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID( object, prop_id, pspec );
			break;
	}
}


/*************************************************************************//**
 * @brief ECNR RXエレメント getプロパティ処理
 * @param[in] object
 * @param[in] prop_id
 * @param[in] value
 * @param[in] pspec
 * @par 説明
 *        上位からの要求によりプロパティを取得する。<br>
 *        - TSB_PARAMプロパティ：      snd_pcm_get__tsb_ecnr_tsb_parameter()をコールし、TSBパラメータ格納アドレスを取得する。
 *        - RX_PARAMプロパティ：       snd_pcm_get__tsb_ecnr_alsasink_parameter()をコールし、RXパラメータ格納アドレスを取得する。
 *        - RX_ECNR_PARAMプロパティ：  snd_pcm_get__tsb_ecnr_RX_ECNR_parameter()をコールし、ECNR初期化状態（'0'=未初期化、'1'=初期化済み）を取得する。
 *****************************************************************************/
static void gst_tsbecnrrx_get_property( GObject *object, guint prop_id, GValue *value, GParamSpec *pspec )
{
	switch ( prop_id )
	{
		case PROP_TSB_PARAM:
			g_value_set_pointer( value, snd_pcm_get__tsb_ecnr_tsb_parameter() );
			break;
		
		case PROP_RX_PARAM:
			g_value_set_pointer( value, snd_pcm_get__tsb_ecnr_alsasink_parameter() );
			break;
		
		case PROP_RX_ECNR_PARAM:
			g_value_set_int( value, snd_pcm_get__tsb_ecnr_RX_ECNR_parameter() );
			break;
		
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID( object, prop_id, pspec );
			break;
	}
}


/* GstElement vmethod implementations */


/* chain function
 * this function does the actual processing
 */
/*************************************************************************//**
 * @brief ECNR RXエレメント SINKパッド処理
 * @param[in] pad
 * @param[in] parent
 * @param[in] buf
 * @par 説明
 *        ECNR RXエレメントのSINKパッドにデータが入力された場合に本関数がコールされる。<br>
 *        - ①SINKに入力されたデータを取得する。
 *        - ②alsa-lib組み込み用snd_pcm_writei ( ECNR RX処理 ) APIをコールし、ECNR処理を行う。
 *        - ③ECNR済みRXデータをSRCパッドに送信する。
 *        - ④TSB_ECNR_GET_RX_DATA_REQUEST or TSB_ECNR_TWO_SRC定義が有効時には、RX参照信号を シグナル送信 or RX参照信号用SRCパッド送信する。
 *****************************************************************************/
#ifndef GST_VER_1_0
static GstFlowReturn gst_tsbecnrrx_chain( GstPad *pad, GstBuffer *buf )
#else  /* GST_VER_1_0 */
static GstFlowReturn gst_tsbecnrrx_chain( GstPad *pad, GstObject *parent, GstBuffer *buf )
#endif  /* GST_VER_1_0 */
{
#ifndef GST_VER_1_0
	Gsttsbecnrrx *filter = GST_TSBECNRRX( GST_PAD_PARENT( pad ) );
#else  /* GST_VER_1_0 */
	Gsttsbecnrrx *filter = GST_TSBECNRRX( parent );
#endif  /* GST_VER_1_0 */
	
	GstFlowReturn ret;
	
	GstBuffer *outbuf = NULL;
	guint i;
	
	gint16 *ecnr_in_data;
	gint16 *ecnr_out_data;
	guint ecnr_in_size;
	
	void *out = NULL;
	unsigned long out_size;
	
#ifdef GST_VER_1_0
	GstMapInfo info_in;
	GstMapInfo info_out;
#endif  /* GST_VER_1_0 */
	
	
	if ( filter->ecnr_switch == FALSE )
	{
		return ( gst_pad_push( filter->srcpad, buf ) );
	}
	
	
	if ( filter->rate == 0 )
	{
		gst_buffer_unref( buf );
		return ( GST_FLOW_NOT_NEGOTIATED );
	}
	
	
	if ( ( filter->nb_mode == 2 ) && ( filter->nb_mode_caps_set_flag == FALSE ) )
	{
		GstCaps *outcaps;
		
		filter->nb_mode_caps_set_flag = TRUE;
		
#ifndef GST_VER_1_0
		outcaps = gst_caps_new_simple(	"audio/x-raw-int",
										"width",		G_TYPE_INT,		16,
										"depth",		G_TYPE_INT,		16,
										"endianness",	G_TYPE_INT,		G_BYTE_ORDER,
										"signed",		G_TYPE_BOOLEAN,	TRUE,
										"rate",			G_TYPE_INT,		16000,
										"channels",		G_TYPE_INT,		1,
										NULL );
#else  /* GST_VER_1_0 */
		outcaps = gst_caps_new_simple(	"audio/x-raw",
										"format",		G_TYPE_STRING,	"S16LE",
										"rate",			G_TYPE_INT,		16000,
										"channels",		G_TYPE_INT,		1,
										NULL );
#endif  /* GST_VER_1_0 */
		
		gst_pad_set_caps( filter->srcpad, outcaps );
		gst_caps_unref( outcaps );
	}
	
	
	/* get input buffer mem,size */
#ifndef GST_VER_1_0
	ecnr_in_data = (gint16*)GST_BUFFER_DATA( buf );
	ecnr_in_size = GST_BUFFER_SIZE( buf );
#else  /* GST_VER_1_0 */
	gst_buffer_map( buf, &info_in, GST_MAP_READ );
	ecnr_in_data = (gint16*)info_in.data;
	ecnr_in_size = (guint)info_in.size;
#endif  /* GST_VER_1_0 */
	
	
	/* ecnr */
	out_size = (unsigned long)( ecnr_in_size >> 1 );
	
	ecnr_in_data = (gint16*)Wrapper_IMX__TSB_ECNR_writei(	"GST_RX_PLUGIN",
															1 /* ch */,
															2 /* S16LE format */,
															ecnr_in_data,
															&out_size );
	
	ecnr_in_size = (gint16)( out_size << 1 );
	
	
	/* sink */
	if ( ( ecnr_in_size > 0 ) && ( ecnr_in_data ) )
	{
		/* alloc output mem */
#ifndef GST_VER_1_0
		ret = gst_pad_alloc_buffer_and_set_caps(	filter->srcpad, GST_BUFFER_OFFSET_NONE, ecnr_in_size,
													GST_PAD_CAPS( filter->srcpad ), &outbuf );
#else  /* GST_VER_1_0 */
		outbuf = gst_buffer_new_allocate( NULL, ecnr_in_size, NULL );
#endif  /* GST_VER_1_0 */
		
#ifndef GST_VER_1_0
		if ( ret == GST_FLOW_OK )
#else  /* GST_VER_1_0 */
		if ( outbuf )
#endif  /* GST_VER_1_0 */
		{
#ifndef GST_VER_1_0
			ecnr_out_data = (gint16*)GST_BUFFER_DATA( outbuf );
#else  /* GST_VER_1_0 */
			gst_buffer_map( outbuf, &info_out, (GstMapFlags)GST_MAP_READWRITE );
			ecnr_out_data = (gint16*)info_out.data;
#endif  /* GST_VER_1_0 */
			
			
			/* set buffer info in -> out */
			gst_tsbecnrrx_set_timestamp( buf, outbuf );
			
#ifndef GST_VER_1_0
			gst_buffer_set_caps( outbuf, GST_PAD_CAPS( filter->srcpad ) );
#endif  /* GST_VER_1_0 */
			
			
			/* ecnr data copy */
			for ( i = 0; i < ( ecnr_in_size >> 1 ); i++ )
			{
				ecnr_out_data[ i ] = ecnr_in_data[ i ];
			}
			
			
#ifdef GST_VER_1_0
			gst_buffer_unmap( outbuf, &info_out );
#endif  /* GST_VER_1_0 */
		}
	}
	
	
	/* input buffer free */
#ifdef GST_VER_1_0
	gst_buffer_unmap( buf, &info_in );
#endif  /* GST_VER_1_0 */
	
	
	/* rx_sink */
#if defined( TSB_ECNR_GET_RX_DATA_REQUEST ) || defined( TSB_ECNR_TWO_SRC )
	if ( out )
	{
		/* ecnr rx -> tx data send */
		GstBuffer *rx_outbuf;
		gint16 *ecnr_rx_out_data;
		gint16 *outdata;
		int rx_out_size;
		void *rx_out;
		
	#ifdef GST_VER_1_0
		GstMapInfo info_rx_out;
	#endif  /* GST_VER_1_0 */
		
		rx_out = snd_pcm_set__rx_data_transfer_send( &rx_out_size );
		
		if ( rx_out && rx_out_size )
		{
			/* alloc output mem */
	#ifndef GST_VER_1_0
			ret = gst_pad_alloc_buffer_and_set_caps(	filter->srcpad, GST_BUFFER_OFFSET_NONE, (guint)( rx_out_size << 1 ),
														GST_PAD_CAPS( filter->srcpad ), &rx_outbuf );
	#else  /* GST_VER_1_0 */
			rx_outbuf = gst_buffer_new_allocate( NULL, (guint)( rx_out_size << 1 ), NULL );
	#endif  /* GST_VER_1_0 */
			
	#ifndef GST_VER_1_0
			if ( ret == GST_FLOW_OK )
	#else  /* GST_VER_1_0 */
			if ( rx_outbuf )
	#endif  /* GST_VER_1_0 */
			{
	#ifndef GST_VER_1_0
				ecnr_rx_out_data = (gint16*)GST_BUFFER_DATA( rx_outbuf );
	#else  /* GST_VER_1_0 */
				gst_buffer_map( rx_outbuf, &info_rx_out, (GstMapFlags)GST_MAP_READWRITE );
				ecnr_rx_out_data = (gint16*)info_rx_out.data;
	#endif  /* GST_VER_1_0 */
				
				/* set buffer info in -> out */
				gst_tsbecnrrx_set_timestamp( buf, rx_outbuf );
				
	#ifndef GST_VER_1_0
				gst_buffer_set_caps( rx_outbuf, GST_PAD_CAPS( filter->srcpad ) );
	#endif  /* GST_VER_1_0 */
				
				
				/* ecnr data copy */
				outdata = (gint16*)rx_out;
				
				for ( i = 0; i < rx_out_size; i++ )
				{
					ecnr_rx_out_data[ i ] = outdata[ i ];
				}
				
	#ifndef TSB_ECNR_TWO_SRC
				g_signal_emit( filter, tsb_ecnr_signals[ GET_DATA_REQUEST_SIGNAL ], 0, rx_outbuf, &ret );
	#else  /* TSB_ECNR_TWO_SRC */
				ret = gst_pad_push( filter->rx_srcpad, rx_outbuf );
	#endif  /* TSB_ECNR_TWO_SRC */
				
	#ifdef GST_VER_1_0
				gst_buffer_unmap( rx_outbuf, &info_rx_out );
	#endif  /* GST_VER_1_0 */
			}
		}
	}
#endif  /* defined( TSB_ECNR_GET_RX_DATA_REQUEST ) || defined( TSB_ECNR_TWO_SRC ) */
	
	
	/* rx reset signal emit */
	if ( snd_pcm_check__rx_reset_request() )
	{
		g_signal_emit( filter, tsb_ecnr_signals[ RX_RESET_REQUEST_SIGNAL ], 0 );
	}
	
	
	/* rx reset check */
	if ( snd_pcm_check__all_reset_request( 0 ) )
	{
		/* rx reset */
		snd_pcm_set__rx_restart();
	}
	
	
	/* push src */
	if ( outbuf )
	{
		gst_buffer_unref( buf );
		ret = gst_pad_push( filter->srcpad, outbuf );
	}
	else
	{
		ret = gst_pad_push( filter->srcpad, buf );
	}
	
	
	return ( ret );
}


/*************************************************************************//**
 * @brief ECNR RXエレメント ステータス処理
 * @param[in] element
 * @param[in] transition
 * @par 説明
 *        GStreamerステータスが変更された場合に本関数がコールされる。<br>
 *        - NULL→READY状態：      ECNR RXエレメント構造体初期化
 *        - READY→PAUSED状態：    ECNR RXエレメント構造体初期化
 *        - PAUSED→PLAYING状態：  何もしない
 *        - PLAYING→PAUSED状態：  何もしない
 *        - PAUSED→READY状態：    ECNR 終了処理を行う。
 *        - READY→NULL状態：      ECNR 終了処理を行う。
 *****************************************************************************/
static GstStateChangeReturn gst_tsbecnrrx_change_state( GstElement *element, GstStateChange transition )
{
	GstStateChangeReturn ret;
	Gsttsbecnrrx *filter = GST_TSBECNRRX( element );
	
	switch ( transition )
	{
		default:
		break;
	}
	
	ret = GST_ELEMENT_CLASS( parent_class )->change_state( element, transition );
	
	if ( ret != GST_STATE_CHANGE_SUCCESS )
	{
		return ret;
	}
	
	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		case GST_STATE_CHANGE_READY_TO_PAUSED:
			filter->rate = 0;
			filter->channels = 0;
			break;
		
		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
			break;
		
		case GST_STATE_CHANGE_PAUSED_TO_READY:
		case GST_STATE_CHANGE_READY_TO_NULL:
			if ( filter->init_flag == TRUE )
			{
				filter->init_flag = FALSE;
				snd_pcm_set__tsb_ecnr_RX_ECNR_parameter( 0 );
			}
			break;
		
		default:
			break;
	}
	
	return ( ret );
}


/*************************************************************************//**
 * @brief ECNR RXエレメント タイムスタンプ作成処理
 * @param[in]  inbuf      タイムスタンプ入力バッファポインタ
 * @param[out] outbuf     タイムスタンプ出力バッファポインタ
 * @par 説明
 *        ECNR RXエレメントのタイムスタンプ作成を行う。
 *****************************************************************************/
static void gst_tsbecnrrx_set_timestamp( GstBuffer *inbuf, GstBuffer *outbuf )
{
	if ( GST_BUFFER_FLAG_IS_SET( inbuf, GST_BUFFER_FLAG_DISCONT ) )
	{
		GST_BUFFER_FLAG_SET( outbuf, GST_BUFFER_FLAG_DISCONT );
	}
	
#ifdef GST_VER_1_0
	if ( GST_BUFFER_FLAG_IS_SET( inbuf, GST_BUFFER_FLAG_RESYNC ) )
	{
		GST_BUFFER_FLAG_SET( outbuf, GST_BUFFER_FLAG_RESYNC );
	}
	GST_BUFFER_PTS( outbuf ) = GST_BUFFER_PTS( inbuf );
	GST_BUFFER_DTS( outbuf ) = GST_BUFFER_DTS( inbuf );
#else  /* GST_VER_1_0 */
	GST_BUFFER_TIMESTAMP( outbuf ) = GST_BUFFER_TIMESTAMP( inbuf );
#endif  /* GST_VER_1_0 */
	
	GST_BUFFER_DURATION( outbuf ) = GST_BUFFER_DURATION( inbuf );
}


/* this function handles sink events */
#ifdef GST_VER_1_0
/*************************************************************************//**
 * @brief ECNR RXエレメント イベント受信処理
 * @param[in] pad
 * @param[in] parent
 * @param[in] event
 * @par 説明
 *        GStreamer1.0専用処理。<br>
 *        イベントを受信した場合に本関数がコールされる。<br>
 *        ケイパビリティイベント発生時に受信したケイパビリティの中からサンプリング周波数、チャンネル数を取得し、<br>
 *        ECNR RXエレメント構造体に保存する。
 *****************************************************************************/
static gboolean gst_tsbecnrrx_sink_event( GstPad *pad, GstObject *parent, GstEvent *event )
{
	int rate;
	int channels;
	gboolean format = FALSE;
	const gchar *format_str;
	gboolean ret;
	Gsttsbecnrrx *filter;
	GstStructure *structure;
	
	filter = GST_TSBECNRRX( parent );
	
	switch ( GST_EVENT_TYPE( event ) )
	{
		case GST_EVENT_CAPS:
		{
			GstCaps *caps;
			
			gst_event_parse_caps( event, &caps );
			/* do something with the caps */
			
			structure = gst_caps_get_structure( caps, 0 );
			
			ret = gst_structure_get_int( structure, "rate", &rate );
			ret &= gst_structure_get_int( structure, "channels", &channels );
			
			if ( ret )
			{
				format_str = gst_structure_get_string( structure, "format" );
				
				if ( format_str )
				{
					if ( g_str_equal( format_str, "S16LE" ) )
					{
						format = TRUE;
					}
				}
				
				if ( ( rate == 24000 || rate == 16000 || rate == 8000 ) && ( channels == 1 ) && ( format == TRUE ) )
				{
					filter->rate = rate;
					filter->channels = channels;
					
					if ( ( filter->rate == 8000 ) && ( filter->nb_mode == 2 ) )
					{
						GstCaps *outcaps = gst_caps_copy( caps );
						
						filter->nb_mode_caps_set_flag = TRUE;
						
						gst_caps_set_simple( outcaps, "rate", G_TYPE_INT, 16000, NULL );
						gst_pad_set_caps( filter->srcpad, outcaps );
						gst_caps_unref( outcaps );
					}
					else
					{
						/* and forward */
						ret = gst_pad_event_default( pad, parent, event );
					}
				}
				else
				{
					filter->rate = 0;
					filter->channels = 0;
					filter->ecnr_switch = FALSE;
					
					/* and forward */
					ret = gst_pad_event_default( pad, parent, event );
				}
			}
			
			break;
		}
		
		default:
			ret = gst_pad_event_default( pad, parent, event );
			break;
	}
	
	return ret;
}
#endif  /* GST_VER_1_0 */


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
/*************************************************************************//**
 * @brief ECNR RXプラグイン 初期化処理
 * @param[in] tsbecnrrx
 * @par 説明
 *        ECNR RXプラグインの初期化を行う。
 *****************************************************************************/
static gboolean tsbecnrrx_init( GstPlugin *tsbecnrrx )
{
	/* debug category for fltering log messages
	 *
	 * exchange the string 'Template tsbecnrrx' with your description
	 */
	GST_DEBUG_CATEGORY_INIT( gst_tsbecnrrx_debug, "tsbecnrrx", 0, "EchoCanceller & NoiseReduction" );
	
	return ( gst_element_register( tsbecnrrx, "tsbecnrrx", GST_RANK_NONE, GST_TYPE_TSBECNRRX ) );
}


/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
	#define PACKAGE "myfirsttsbecnrrx"
#endif  /* PACKAGE */

/* gstreamer looks for this structure to register tsbecnrrxs
 *
 * exchange the string 'Template tsbecnrrx' with your tsbecnrrx description
 */
#ifndef GST_VER_1_0
	GST_PLUGIN_DEFINE
	(
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		"tsbecnrrx",
		"EchoCanceller & NoiseReduction",
		tsbecnrrx_init,
		VERSION,
		"LGPL",
		"GStreamer",
		"http://gstreamer.net/"
	)
#else  /* GST_VER_1_0 */
	GST_PLUGIN_DEFINE
	(
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		tsbecnrrx,
		"EchoCanceller & NoiseReduction",
		tsbecnrrx_init,
		VERSION,
		"LGPL",
		"GStreamer",
		"http://gstreamer.net/"
	)
#endif  /* GST_VER_1_0 */

