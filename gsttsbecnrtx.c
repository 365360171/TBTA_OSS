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
 * @file gsttsbecnrtx.c
 * @brief GStreamer ECNR TXプラグイン
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include <gst/gst.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gsttsbecnrtx.h"

typedef unsigned long snd_pcm_uframes_t;	/**< ダミー定義：alsa-lib snd_pcm_writei()引数で使用 */
typedef long snd_pcm_sframes_t;				/**< ダミー定義：alsa-lib snd_pcm_readi()引数で使用 */
#include "pcm_tsb_ecnr.h"

GST_DEBUG_CATEGORY_STATIC( gst_tsbecnrtx_debug );

#define GST_CAT_DEFAULT gst_tsbecnrtx_debug


/* Filter signals and args */
enum
{
	UNMUTE_REQUEST_SIGNAL,		/**< アンミュート要求シグナル */
	TX_RESET_REQUEST_SIGNAL,	/**< TX処理初期化要求シグナル */
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_TSB_PARAM,				/**< TSBパラメータ設定プロパティ */
	PROP_TX_PARAM,				/**< TXパラメータ設定プロパティ */
	PROP_TX_ECNR_PARAM,			/**< ECNR TXエレメント 初期化・終了プロパティ */
	PROP_SPKVOL_CHANGE_NOTIFY,	/**< スピーカボリューム変更通知プロパティ */
	PROP_TX_SET_RXDATA			/**< RX参照信号設定プロパティ */
};

static guint tsb_ecnr_signals[ LAST_SIGNAL ] = { 0 };


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


#define gst_tsbecnrtx_parent_class parent_class
G_DEFINE_TYPE( Gsttsbecnrtx, gst_tsbecnrtx, GST_TYPE_ELEMENT );

static void gst_tsbecnrtx_set_property( GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec );
static void gst_tsbecnrtx_get_property( GObject *object, guint prop_id, GValue *value, GParamSpec *pspec );

static GstStateChangeReturn gst_tsbecnrtx_change_state( GstElement *element, GstStateChange transition );

#ifndef GST_VER_1_0
	static GstFlowReturn gst_tsbecnrtx_chain( GstPad *pad, GstBuffer *buf );
#else  /* GST_VER_1_0 */
	static GstFlowReturn gst_tsbecnrtx_chain( GstPad *pad, GstObject *parent, GstBuffer *buf );
#endif  /* GST_VER_1_0 */

#ifdef GST_VER_1_0
	static gboolean gst_tsbecnrtx_sink_event( GstPad *pad, GstObject *parent, GstEvent *event );
#endif  /* GST_VER_1_0 */
static void gst_tsbecnrtx_set_timestamp( GstBuffer *inbuf, GstBuffer *outbuf );


/* GObject vmethod implementations */

/* initialize the tsbecnrtx's class */
/*************************************************************************//**
 * @brief ECNR TXエレメント 初期化処理
 * @param[in] klass
 * @par 説明
 *        ECNR TXエレメントの初期化を行う。<br>
 *        - set/getプロパティ登録
 *        - シグナル登録
 *        - ケイパビリティ登録
 *****************************************************************************/
static void gst_tsbecnrtx_class_init( GsttsbecnrtxClass *klass )
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
	
	gobject_class = (GObjectClass*)klass;
	gstelement_class = (GstElementClass*)klass;
	
	gobject_class->set_property = gst_tsbecnrtx_set_property;
	gobject_class->get_property = gst_tsbecnrtx_get_property;
	
	g_object_class_install_property(	gobject_class,
										PROP_TSB_PARAM,
										g_param_spec_pointer(	"TSB_PARAM",
																"TSB PARAM",
																"TSB parameter",
																G_PARAM_READWRITE ) );
	
	g_object_class_install_property(	gobject_class,
										PROP_TX_PARAM,
										g_param_spec_pointer(	"TX_PARAM",
																"TX PARAM",
																"TX parameter",
																G_PARAM_READWRITE ) );
	
	g_object_class_install_property(	gobject_class,
										PROP_TX_ECNR_PARAM,
										g_param_spec_int(	"TX_ECNR_PARAM",
															"TX ECNR PARAM",
															"TX ecnr param",
															G_MININT,
															G_MAXINT,
															0,
															G_PARAM_READWRITE ) );
	
	g_object_class_install_property(	gobject_class,
										PROP_SPKVOL_CHANGE_NOTIFY,
										g_param_spec_boolean(	"TX_SPKVOL_CHANGE_NOTIFY",
																"TX SPKVOL CHANGE NOTIFY",
																"TX spkvol change notify",
																TRUE,
																G_PARAM_WRITABLE ) );
	
	g_object_class_install_property(	gobject_class,
										PROP_TX_SET_RXDATA,
										g_param_spec_pointer(	"TX_SET_RXDATA",
																"TX SET RXDATA",
																"TX set rxdata",
																G_PARAM_WRITABLE ) );
	
	tsb_ecnr_signals[ UNMUTE_REQUEST_SIGNAL ] = g_signal_new(	"ECNR_TX_UNMUTE_REQUEST",
																G_TYPE_FROM_CLASS( klass ),
																G_SIGNAL_RUN_LAST,
																0,
																0,
																NULL,
																g_cclosure_marshal_VOID__VOID,
																G_TYPE_NONE,
																0 );
	
	tsb_ecnr_signals[ TX_RESET_REQUEST_SIGNAL ] = g_signal_new(	"ECNR_TX_RESET_REQUEST",
																G_TYPE_FROM_CLASS( klass ),
																G_SIGNAL_RUN_LAST,
																0,
																0,
																NULL,
																g_cclosure_marshal_VOID__VOID,
																G_TYPE_NONE,
																0 );
	
	gst_element_class_set_details_simple(	gstelement_class,
											"Toshiba ECNR TX Proc",
											"Filter/Convert/Audio",
											"EchoCanceller & NoiseReduction",
											"TOSHIBA Corporation." );
	
	gst_element_class_add_pad_template( gstelement_class, gst_static_pad_template_get( &src_factory ) );
	gst_element_class_add_pad_template( gstelement_class, gst_static_pad_template_get( &sink_factory ) );
	
	gstelement_class->change_state = GST_DEBUG_FUNCPTR( gst_tsbecnrtx_change_state );
	
	return;
}


#ifndef GST_VER_1_0
/*************************************************************************//**
 * @brief ECNR TXエレメント ケイパビリティ受信処理
 * @param[in] pad
 * @param[in] caps
 * @par 説明
 *        GStreamer0.10専用処理。<br>
 *        ケイパビリティを受信した場合に本関数がコールされる。<br>
 *        ケイパビリティの中からサンプリング周波数、チャンネル数を取得し、<br>
 *        ECNR TXエレメント構造体に保存する。
 *****************************************************************************/
static gboolean gst_tsbecnrtx_setcaps( GstPad *pad, GstCaps *caps )
{
	Gsttsbecnrtx *filter;
	GstStructure *structure;
	int rate;
	int channels;
	gboolean format = FALSE;
	const gchar *format_str;
	gboolean ret;
	GstCaps *outcaps;
	
	filter = GST_TSBECNRTX( GST_PAD_PARENT( pad ) );
	
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
		
#ifdef	USE_TEKAPO_DMB
		if ( ( rate == 24000 || rate == 16000 || rate == 8000 ) && ( channels == 1 || channels == 2 ) && ( format == TRUE ) )
#else	/* USE_TEKAPO_DMB */
		if ( ( rate == 24000 || rate == 16000 || rate == 8000 ) && ( channels == 1 ) && ( format == TRUE ) )
#endif	/* USE_TEKAPO_DMB */
		{
			filter->rate = rate;
			filter->channels = channels;
			
			if ( ( filter->rate == 16000 ) && ( filter->nb_mode == 2 ) )
			{
				filter->nb_mode_caps_set_flag = TRUE;
				rate = 8000;
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
	gst_caps_unref( outcaps );
	
	return ( ret );
}
#endif  /* GST_VER_1_0 */


#ifndef GST_VER_1_0
/*************************************************************************//**
 * @brief ECNR TXエレメント ケイパビリティ送信処理
 * @param[in] pad
 * @param[in] caps
 * @par 説明
 *        GStreamer0.10専用処理。<br>
 *        ケイパビリティの取得要求を受けた場合に本関数がコールされる。<br>
 *        ECNR処理によりケイパビリティが変化しないので、特に何もしていない。
 *****************************************************************************/
static GstCaps *gst_tsbecnrtx_getcaps( GstPad *pad )
{
	Gsttsbecnrtx *filter;
	GstPad *otherpad;
	GstCaps *othercaps;
	GstCaps *result;
	const GstCaps *templ;
	const gchar *name;
	gint i;
	
	filter = GST_TSBECNRTX( GST_PAD_PARENT( pad ) );
	
	if ( pad == filter->srcpad )
	{
		name = "audio/x-raw-int";
		otherpad = filter->sinkpad;
	}
	else
	{
		name = "audio/x-raw-int";
		otherpad = filter->srcpad;
	}
	
	othercaps = gst_pad_peer_get_caps( otherpad );
	
	templ = gst_pad_get_pad_template_caps( pad );
	
	if ( othercaps )
	{
		othercaps = gst_caps_make_writable( othercaps );
		
		for ( i = 0; i < gst_caps_get_size( othercaps ); i++ )
		{
			GstStructure *structure;
			
			structure = gst_caps_get_structure( othercaps, i );
			
			gst_structure_set_name( structure, name );
			
			if ( pad == filter->sinkpad )
			{
				gst_structure_remove_fields( structure, "width", "depth", "endianness", "signed", NULL );
			}
			else
			{
				gst_structure_set(	structure,
									"width", G_TYPE_INT, 16,
									"depth", G_TYPE_INT, 16,
									"endianness", G_TYPE_INT, G_BYTE_ORDER,
									"signed", G_TYPE_BOOLEAN, TRUE,
									NULL );
			}
		}
		
		result = gst_caps_intersect( othercaps, templ );
		gst_caps_unref( othercaps );
	}
	else
	{
		result = gst_caps_copy( templ );
	}
	
	return ( result );
}
#endif  /* GST_VER_1_0 */


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
/*************************************************************************//**
 * @brief ECNR TXエレメント パッド初期化処理
 * @param[in] filter ECNR TXエレメント構造体
 * @par 説明
 *        ECNR TXエレメント構造体、SINK/SRCパッドの初期化を行う。
 *****************************************************************************/
static void gst_tsbecnrtx_init( Gsttsbecnrtx *filter )
{
	filter->sinkpad = gst_pad_new_from_static_template( &sink_factory, "sink" );
	
#ifndef GST_VER_1_0
	gst_pad_set_setcaps_function( filter->sinkpad, GST_DEBUG_FUNCPTR( gst_tsbecnrtx_setcaps ) );
	gst_pad_set_getcaps_function( filter->sinkpad, GST_DEBUG_FUNCPTR( gst_tsbecnrtx_getcaps ) );
#endif  /* GST_VER_1_0 */
	
	gst_pad_set_chain_function( filter->sinkpad, GST_DEBUG_FUNCPTR( gst_tsbecnrtx_chain ) );
	
#ifdef GST_VER_1_0
	gst_pad_set_event_function( filter->sinkpad, GST_DEBUG_FUNCPTR( gst_tsbecnrtx_sink_event ) );
#endif  /* GST_VER_1_0 */
	
#ifdef GST_VER_1_0
	GST_PAD_SET_PROXY_CAPS( filter->sinkpad );
#endif  /* GST_VER_1_0 */
	
	gst_element_add_pad( GST_ELEMENT( filter ), filter->sinkpad );
	
	
	filter->srcpad = gst_pad_new_from_static_template( &src_factory, "src" );
	
#ifndef GST_VER_1_0
	gst_pad_set_getcaps_function( filter->srcpad, GST_DEBUG_FUNCPTR( gst_tsbecnrtx_getcaps ) );
#endif  /* GST_VER_1_0 */
	
#ifdef GST_VER_1_0
	GST_PAD_SET_PROXY_CAPS( filter->srcpad );
#endif  /* GST_VER_1_0 */
	
	gst_element_add_pad( GST_ELEMENT( filter ), filter->srcpad );
	
	filter->init_flag = FALSE;
	filter->ecnr_switch = FALSE;
	filter->rate = 0;
	filter->channels = 0;
	filter->nb_mode = 0;
	filter->nb_mode_caps_set_flag = FALSE;
	
	return;
}


/*************************************************************************//**
 * @brief ECNR TXエレメント setプロパティ処理
 * @param[in] object
 * @param[in] prop_id
 * @param[in] value
 * @param[in] pspec
 * @par 説明
 *        上位からの要求によりプロパティを設定する。<br>
 *        - TSB_PARAMプロパティ：      snd_pcm_set__tsb_ecnr_TSB_parameter()をコールし、TSBパラメータを設定する。
 *        - TX_PARAMプロパティ：       snd_pcm_set__tsb_ecnr_TX_parameter()をコールし、TXパラメータを設定する。
 *        - TX_ECNR_PARAMプロパティ：  設定値が'1'の場合にはECNR初期化を行い、'0'の場合にはECNR終了処理を行う。
 *        - TX_SET_RXDATAプロパティ：  snd_pcm_set__rx_data_transfer_receive()をコールし、RX参照信号を受け取る。
 *****************************************************************************/
static void gst_tsbecnrtx_set_property( GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec )
{
	Gsttsbecnrtx *filter = GST_TSBECNRTX( object );
	
	switch ( prop_id )
	{
		case PROP_TSB_PARAM:
			snd_pcm_set__tsb_ecnr_TSB_parameter( g_value_get_pointer( value ) );
			break;
		
		case PROP_TX_PARAM:
			snd_pcm_set__tsb_ecnr_TX_parameter( g_value_get_pointer( value ) );
			break;
		
		case PROP_TX_ECNR_PARAM:
			if ( g_value_get_int( value ) )
			{
				TSBECNR_TX_PARAM_t *tx_param = NULL;
				
				filter->init_flag = TRUE;
				filter->ecnr_switch = TRUE;
				
#ifndef VCPP
				snd_pcm_set__rx_data_transfer_method( 0 );
#else  /* VCPP */
				snd_pcm_set__rx_data_transfer_method( 1 );
#endif  /* VCPP */
				
				snd_pcm_set__tsb_ecnr_TX_ECNR_parameter( 1 );
				
				tx_param = (TSBECNR_TX_PARAM_t*)snd_pcm_get__tsb_ecnr_alsasrc_parameter();
				
				if ( tx_param )
				{
					if ( ( tx_param->TX_SRC_IN_FS == 16000 ) && ( tx_param->TX_SRC_OUT_FS == 8000 ) )
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
				
				snd_pcm_set__tsb_ecnr_TX_ECNR_parameter( 0 );
			}
			break;
		
		case PROP_SPKVOL_CHANGE_NOTIFY:
			snd_pcm_set__SpkVolChange_Notify();
			break;
		
		case PROP_TX_SET_RXDATA:
			{
				GstBuffer *buf = (GstBuffer*)g_value_get_pointer( value );
				gint16 *ecnr_in_data;
				guint ecnr_in_size;
				
				if ( filter->init_flag == TRUE && buf )
				{
					/* get input buffer mem,size */
#ifndef GST_VER_1_0
					ecnr_in_data = (gint16*)GST_BUFFER_DATA( buf );
					ecnr_in_size = GST_BUFFER_SIZE( buf );
#else  /* GST_VER_1_0 */
					GstMapInfo info;
					gst_buffer_map( buf, &info, GST_MAP_READ );
					ecnr_in_data = (gint16*)info.data;
					ecnr_in_size = (guint)info.size;
#endif  /* GST_VER_1_0 */
					
					/* ecnr */
					snd_pcm_set__rx_data_transfer_receive( (void*)ecnr_in_data, ( ecnr_in_size >> 1 ) );
					
#ifdef GST_VER_1_0
					gst_buffer_unmap( buf, &info );
#endif  /* GST_VER_1_0 */
				}
				
				gst_buffer_unref( buf );
			}
			break;
		
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID( object, prop_id, pspec );
			break;
	}
}


/*************************************************************************//**
 * @brief ECNR TXエレメント getプロパティ処理
 * @param[in] object
 * @param[in] prop_id
 * @param[in] value
 * @param[in] pspec
 * @par 説明
 *        上位からの要求によりプロパティを取得する。<br>
 *        - TSB_PARAMプロパティ：      snd_pcm_get__tsb_ecnr_tsb_parameter()をコールし、TSBパラメータ格納アドレスを取得する。
 *        - TX_PARAMプロパティ：       snd_pcm_get__tsb_ecnr_alsasrc_parameter()をコールし、TXパラメータ格納アドレスを取得する。
 *        - TX_ECNR_PARAMプロパティ：  snd_pcm_get__tsb_ecnr_TX_ECNR_parameter()をコールし、ECNR初期化状態（'0'=未初期化、'1'=初期化済み）を取得する。
 *****************************************************************************/
static void gst_tsbecnrtx_get_property( GObject *object, guint prop_id, GValue *value, GParamSpec *pspec )
{
	switch ( prop_id )
	{
		case PROP_TSB_PARAM:
			g_value_set_pointer( value, snd_pcm_get__tsb_ecnr_tsb_parameter() );
			break;
		
		case PROP_TX_PARAM:
			g_value_set_pointer( value, snd_pcm_get__tsb_ecnr_alsasrc_parameter() );
			break;
		
		case PROP_TX_ECNR_PARAM:
			g_value_set_int( value, snd_pcm_get__tsb_ecnr_TX_ECNR_parameter() );
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
 * @brief ECNR TXエレメント SINKパッド処理
 * @param[in] pad
 * @param[in] parent
 * @param[in] buf
 * @par 説明
 *        ECNR TXエレメントのSINKパッドにデータが入力された場合に本関数がコールされる。<br>
 *        - ①SINKに入力されたデータを取得する。
 *        - ②alsa-lib組み込み用snd_pcm_readi ( ECNR TX処理 ) APIをコールし、ECNR処理を行う。
 *        - ③ECNR済みTXデータをSRCパッドに送信する。
 *****************************************************************************/
#ifndef GST_VER_1_0
static GstFlowReturn gst_tsbecnrtx_chain( GstPad *pad, GstBuffer *buf )
#else  /* GST_VER_1_0 */
static GstFlowReturn gst_tsbecnrtx_chain( GstPad *pad, GstObject *parent, GstBuffer *buf )
#endif  /* GST_VER_1_0 */
{
#ifndef GST_VER_1_0
	Gsttsbecnrtx *filter = GST_TSBECNRTX( GST_PAD_PARENT( pad ) );
#else  /* GST_VER_1_0 */
	Gsttsbecnrtx *filter = GST_TSBECNRTX( parent );
#endif  /* GST_VER_1_0 */
	
	GstFlowReturn ret;
	
	GstBuffer *outbuf = NULL;
	guint i;
	
	gint16 *ecnr_in_data;
	gint16 *ecnr_out_data;
	guint ecnr_in_size;
	
	long result;
	
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
										"rate",			G_TYPE_INT,		8000,
										"channels",		G_TYPE_INT,		1,
										NULL );
#else  /* GST_VER_1_0 */
		outcaps = gst_caps_new_simple(	"audio/x-raw",
										"format",		G_TYPE_STRING,	"S16LE",
										"rate",			G_TYPE_INT,		8000,
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
	result = (long)( ecnr_in_size >> 1 );
#ifdef	USE_TEKAPO_DMB
	Wrapper_IMX__TSB_ECNR_readi( "GST_TX_PLUGIN", 2 /* ch */, 2 /* S16LE format */, ecnr_in_data, &result );
	result <<= 1;
#else	/* USE_TEKAPO_DMB */
	Wrapper_IMX__TSB_ECNR_readi( "GST_TX_PLUGIN", 1 /* ch */, 2 /* S16LE format */, ecnr_in_data, &result );
#endif	/* USE_TEKAPO_DMB */
	ecnr_in_size = (gint16)( result << 1 );
	
	
	/* sink */
	if ( ecnr_in_size > 0 )
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
			gst_tsbecnrtx_set_timestamp( buf, outbuf );
			
#ifndef GST_VER_1_0
			gst_buffer_set_caps( outbuf, GST_PAD_CAPS( filter->srcpad ) );
#endif  /* GST_VER_1_0 */
			
			
			/* ecnr data copy */
			for ( i = 0; i < ( ecnr_in_size >> 1 ); i++ )
			{
#ifdef	USE_TEKAPO_DMB
				ecnr_out_data[ i ] = ecnr_in_data[ ( i / 2 ) ];
#else	/* USE_TEKAPO_DMB */
				ecnr_out_data[ i ] = ecnr_in_data[ i ];
#endif	/* USE_TEKAPO_DMB */
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
	
	
	/* unmute signal emit */
	if ( snd_pcm_check__unmute_request() )
	{
		g_signal_emit( filter, tsb_ecnr_signals[ UNMUTE_REQUEST_SIGNAL ], 0 );
	}
	
	
	/* tx reset signal emit */
	if ( snd_pcm_check__tx_reset_request() )
	{
		g_signal_emit( filter, tsb_ecnr_signals[ TX_RESET_REQUEST_SIGNAL ], 0 );
	}
	
	
	/* tx reset check */
	if ( snd_pcm_check__all_reset_request( 1 ) )
	{
		/* tx reset */
		snd_pcm_set__tx_restart();
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
 * @brief ECNR TXエレメント ステータス処理
 * @param[in] element
 * @param[in] transition
 * @par 説明
 *        GStreamerステータスが変更された場合に本関数がコールされる。<br>
 *        - NULL→READY状態：      ECNR TXエレメント構造体初期化
 *        - READY→PAUSED状態：    ECNR TXエレメント構造体初期化
 *        - PAUSED→PLAYING状態：  何もしない
 *        - PLAYING→PAUSED状態：  何もしない
 *        - PAUSED→READY状態：    ECNR 終了処理を行う。
 *        - READY→NULL状態：      ECNR 終了処理を行う。
 *****************************************************************************/
static GstStateChangeReturn gst_tsbecnrtx_change_state( GstElement *element, GstStateChange transition )
{
	GstStateChangeReturn ret;
	Gsttsbecnrtx *filter = GST_TSBECNRTX( element );
	
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
				snd_pcm_set__tsb_ecnr_TX_ECNR_parameter( 0 );
			}
			break;
		
		default:
			break;
	}
	
	return ( ret );
}


/*************************************************************************//**
 * @brief ECNR TXエレメント タイムスタンプ作成処理
 * @param[in]  inbuf      タイムスタンプ入力バッファポインタ
 * @param[out] outbuf     タイムスタンプ出力バッファポインタ
 * @par 説明
 *        ECNR TXエレメントのタイムスタンプ作成を行う。
 *****************************************************************************/
static void gst_tsbecnrtx_set_timestamp( GstBuffer *inbuf, GstBuffer *outbuf )
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
 * @brief ECNR TXエレメント イベント受信処理
 * @param[in] pad
 * @param[in] parent
 * @param[in] event
 * @par 説明
 *        GStreamer1.0専用処理。<br>
 *        イベントを受信した場合に本関数がコールされる。<br>
 *        ケイパビリティイベント発生時に受信したケイパビリティの中からサンプリング周波数、チャンネル数を取得し、<br>
 *        ECNR TXエレメント構造体に保存する。
 *****************************************************************************/
static gboolean gst_tsbecnrtx_sink_event( GstPad *pad, GstObject *parent, GstEvent *event )
{
	int rate;
	int channels;
	gboolean format = FALSE;
	const gchar *format_str;
	gboolean ret;
	Gsttsbecnrtx *filter;
	GstStructure *structure;
	
	filter = GST_TSBECNRTX( parent );
	
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
				
#ifdef	USE_TEKAPO_DMB
				if ( ( rate == 24000 || rate == 16000 || rate == 8000 ) && ( channels == 1 || channels == 2 ) && ( format == TRUE ) )
#else	/* USE_TEKAPO_DMB */
				if ( ( rate == 24000 || rate == 16000 || rate == 8000 ) && ( channels == 1 ) && ( format == TRUE ) )
#endif	/* USE_TEKAPO_DMB */
				{
					filter->rate = rate;
					filter->channels = channels;
					
					if ( ( filter->rate == 16000 ) && ( filter->nb_mode == 2 ) )
					{
						GstCaps *outcaps = gst_caps_copy( caps );
						
						filter->nb_mode_caps_set_flag = TRUE;
						
						gst_caps_set_simple( outcaps, "rate", G_TYPE_INT, 8000, NULL );
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
 * @brief ECNR TXプラグイン 初期化処理
 * @param[in] tsbecnrtx
 * @par 説明
 *        ECNR TXプラグインの初期化を行う。
 *****************************************************************************/
static gboolean tsbecnrtx_init( GstPlugin *tsbecnrtx )
{
	/* debug category for fltering log messages
	 *
	 * exchange the string 'Template tsbecnrtx' with your description
	 */
	GST_DEBUG_CATEGORY_INIT( gst_tsbecnrtx_debug, "tsbecnrtx", 0, "EchoCanceller & NoiseReduction" );
	
	return ( gst_element_register( tsbecnrtx, "tsbecnrtx", GST_RANK_NONE, GST_TYPE_TSBECNRTX ) );
}


/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
	#define PACKAGE "myfirsttsbecnrtx"
#endif  /* PACKAGE */

/* gstreamer looks for this structure to register tsbecnrtxs
 *
 * exchange the string 'Template tsbecnrtx' with your tsbecnrtx description
 */
#ifndef GST_VER_1_0
	GST_PLUGIN_DEFINE
	(
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		"tsbecnrtx",
		"EchoCanceller & NoiseReduction",
		tsbecnrtx_init,
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
		tsbecnrtx,
		"EchoCanceller & NoiseReduction",
		tsbecnrtx_init,
		VERSION,
		"LGPL",
		"GStreamer",
		"http://gstreamer.net/"
	)
#endif  /* GST_VER_1_0 */

