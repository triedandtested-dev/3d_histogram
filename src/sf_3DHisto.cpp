/*
 *      Name:   sf_3DHisto.cpp
 *
 *      Author: Bryan 'Fox' Dunkley
 *
 *      Description:
 *              This in a nuke node that generates a 3 deminsional histogram based on the inputs resolution
 *               and calucated value which drives the y-axis of the histogram.
 *
 */
 
#include <iostream>
#include <cstring>
#include <sstream>
#include <vector>
#include <cmath>

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/gl.h"
#include "DDImage/ViewerContext.h"
#include "DDImage/Enumeration_KnobI.h"

#include "XYZtoCorColorTemp.h"

#define VERSION "3D_Histo v2.0"
#define VERSION_STR "Developed by: Bryan \"fox\" Dunkley"

static const char* const CLASS = "sf_3DHisto";
static const char* const HELP =
  "3D Histogram.\n\n"
  "This displays image color information in 3d.\n\n"
  "Developed by: Bryan \"fox\" Dunkley";

using namespace DD::Image;
using namespace std;

struct ColorStruct
{
	float value1;
	float value2;
	float value3;
};

float sRGB_matrix[3][3] = {0.4124564, 0.3575761, 0.1804375,
				0.2126729, 0.7151522, 0.0721750,
				0.0193339, 0.1191920, 0.9503041};

void RGBtoXYZ(float r, float g, float b, float* XYZresult)
{
	//RGB * sRGB_matrix r = XYZresult[0], g = XYZresult[1], b = XYZresult[2]

	XYZresult[0] = r * sRGB_matrix[0][0] + g * sRGB_matrix[1][0] + b * sRGB_matrix[2][0];
	XYZresult[1] = r * sRGB_matrix[0][1] + g * sRGB_matrix[1][1] + b * sRGB_matrix[2][1];
	XYZresult[2] = r * sRGB_matrix[0][2] + g * sRGB_matrix[1][2] + b * sRGB_matrix[2][2];
}

static const char* const histo_list[] = {
  "luminance", "hsv.hue", "hsv.saturation", "hsv.value", "tmi.temperature", "tmi.magenta", "tmi.intensity", "channel", 0
};

static const char* const render_list[] = {
  "Lines", "Points", 0
};

static const char* const default_channel_list[] = {"" , 0};

class sf_3DHisto : public Iop
{
	private:

		//Private variables
		float size;
		int width;
		int height;
		float histoheight;
		int histoDisplay;
		int histoRender;
		bool useOverlays;
		float overlaySize;
		float overlay2Colour[3];
		float overlay1Colour[3];
		float overlay2_Value;
		float overlay1_Value;
		float overlay2_Opacity;
		float overlay1_Opacity;
		bool overlay1_ON;
		bool overlay2_ON;
		bool overlay1_SELECTABLE;
		bool overlay2_SELECTABLE;
		GLuint pointcloud_displaylist;
		int channel_selection;
		std::vector<std::string> channel_list;
		bool disable_flag;

		//Private functions
		float getHistoValue(float r, float g, float b, int operation); //for operations requiring 3 inputs (Luminance, Saturation)
		float getHistoValue(float value); //for operations requiring 1 input (single channels)
		ColorStruct RBGtoHSV(float rawr, float rawg, float rawb);
		float calculateCT(float r, float g, float b);
		void normalHisto(); //default drawing of HSV, L, TMI
		void customHisto(); //draw specified channel
		void drawOverlays(); //draw range overlays

	public:
	#if DD_IMAGE_VERSION_MAJOR >= 5
		sf_3DHisto(Node* node) : Iop(node)
	#else
		sf_3DHisto() : Iop()
	#endif
		{
			//default values for knobs
			size = 3.0;
			histoDisplay = 2;
			histoRender = 1;
			histoheight = 100.0f;
			useOverlays = false;
			overlaySize = 30;
			overlay1Colour[0] = 0.3f;
			overlay1Colour[1] = 0;
			overlay1Colour[2] = 0;

			overlay2Colour[0] = 0;
			overlay2Colour[1] = 0;
			overlay2Colour[2] = 0.3;

			overlay2_Value = 0.0f;
			overlay1_Value = 1.0f;
			overlay1_Opacity = overlay2_Opacity = 0.5f;
			overlay1_ON = overlay2_ON = true;
			overlay1_SELECTABLE = overlay2_SELECTABLE = true;

			width = 0;
			height = 0;
			channel_selection = 0;
			pointcloud_displaylist = glGenLists(1);
			disable_flag = false;
			inputs(1);
		}
		void channelListGenerator();
		virtual void knobs(Knob_Callback);
		int knob_changed(Knob* k);
		const char* Class() const { return CLASS; }
		const char* node_help() const { return HELP; }
		const char* input_label(int input, char* buffer) const;
		static const Iop::Description d;
		bool handle(ViewerContext* ctx, int index);
		bool isOverlay1Selectable();
		bool isOverlay2Selectable();
		bool OverlaysActive();
		float Overlay1Value();
		float Overlay2Value();
		float getHistoHeight();
		int getHeight();
		int getWidth();
		void build_handles(ViewerContext* ctx);
		void draw_handle(ViewerContext* ctx);
		void _validate(bool);
		void _request(int x, int y, int r, int t, ChannelMask, int count);
		void engine(int y, int x, int r, ChannelMask channels, Row& out);

	Op::HandlesMode doAnyHandles(ViewerContext* ctx)
	{
		if (ctx->transform_mode() != VIEWER_2D)
		{
			return eHandlesCooked;
		}
		else
		{
			return  Iop::doAnyHandles(ctx);
		}
	}

	class GlueKnob : public Knob
	{
		sf_3DHisto* theOp;
		const char* Class() const {return "Glue";}
		public:

			// This is what Nuke will call once the below stuff is executed:
			static bool handle_cb(ViewerContext* ctx, Knob* knob, int index)
			{
				return ((GlueKnob*)knob)->theOp->handle(ctx, index);
			}

			// Nuke calls this to draw the handle, this then calls make_handle
			// which tells Nuke to call the above function when the mouse does
			// something...
			void draw_handle(ViewerContext* ctx)
			{
				if (theOp->useOverlays)
				{
					if (theOp->isOverlay1Selectable() && theOp->overlay1_ON)
					{
						begin_handle(SELECTABLE, ctx, handle_cb, 0, theOp->getHeight()/2, theOp->Overlay1Value() * theOp->getHistoHeight(), theOp->getWidth()/2, ViewerContext::kCrossCursor);
							if ( is_selected( ctx, handle_cb, 0))
							{
								glColor3f(1.0f,0.0f,0.0f);
							}
							else
							{
								glColor3f(0.68f,0.68f,0.68f);
							}

							glPointSize(6.0f);
							glBegin(GL_POINTS);
								glVertex3f(theOp->getHeight()/2, theOp->Overlay1Value() * theOp->getHistoHeight(), theOp->getWidth()/2);
							glEnd();
						end_handle(ctx);
					}

					if (theOp->isOverlay2Selectable() && theOp->overlay2_ON)
					{
						begin_handle(SELECTABLE, ctx, handle_cb, 1, theOp->getHeight()/2, theOp->Overlay2Value() * theOp->getHistoHeight(), theOp->getWidth()/2, ViewerContext::kCrossCursor);
							if ( is_selected( ctx, handle_cb, 1))
							{
								glColor3f(1.0f,0.0f,0.0f);
							}
							else
							{
								glColor3f(0.68f,0.68f,0.68f);
							}

							glPointSize(6.0f);
							glBegin(GL_POINTS);
								glVertex3f(theOp->getHeight()/2, theOp->Overlay2Value() * theOp->getHistoHeight(), theOp->getWidth()/2);
							glEnd();
						end_handle(ctx);
					}
				}
		}

			// And you need to implement this just to make it call draw_handle:
			bool build_handle(ViewerContext* ctx)
			{
			   return (ctx->transform_mode() != VIEWER_2D);
			}

			GlueKnob(Knob_Closure* kc, sf_3DHisto* t, const char* n) : Knob(kc,n)
			{
			  theOp = t;
			}

		  };
};

////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////
float sf_3DHisto::getHistoValue(float r, float g, float b, int operation)
{
	//"luminamce", "hsv.hue", "hsv.saturation", "hsv.value", "tmi.temperature", "tmi.magenta", "tmi.intensity", "channel"
	//cout << r << ":" << g << ":" << b << endl;

	float value = 0;

	ColorStruct HSV = RBGtoHSV(r, g, b);

	if (operation == 0) //luminance
	{
		value = (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
	}
	else if (operation == 1) //hue
	{
		value = HSV.value1;
	}
	else if (operation == 2) //saturation
	{
		value = HSV.value2;
	}
	else if (operation == 3) //value
	{
		value = HSV.value3;
	}
	else if (operation == 4) //temperature
	{
		value = calculateCT(r, g, b);
	}
	else if (operation == 5) //magenta
	{
		value = 1.0f - g;
	}
	else if (operation == 6) //intensity
	{
		value = r*0.2989 + g*0.5870 + b*0.1140;
	}


	return value;
}

float sf_3DHisto::getHistoValue(float value)
{
	return value;
}

ColorStruct sf_3DHisto::RBGtoHSV(float r, float g, float b)
{
	ColorStruct HSV;
	// RGB are from 0..1, H is from 0..360, SV from 0..1

	double maxC = b;
	if (maxC < g) maxC = g;
	if (maxC < r) maxC = r;
	double minC = b;
	if (minC > g) minC = g;
	if (minC > r) minC = r;

	double delta = maxC - minC;

	double V = maxC;
	double S = 0;
	double H = 0;

	if (delta == 0)
	{
		H = 0;
		S = 0;
	}
	else
	{
		S = delta / maxC;
		double dR = 60*(maxC - r)/delta + 180;
		double dG = 60*(maxC - g)/delta + 180;
		double dB = 60*(maxC - b)/delta + 180;
		if (r == maxC)
			H = dB - dG;
		else if (g == maxC)
			H = 120 + dR - dB;
		else
			H = 240 + dG - dR;
	}

	if (H<0)
		H+=360;
	if (H>=360)
		H-=360;

	HSV.value1 = H / 360.f; //put value between 0..1
	HSV.value2 = S;
	HSV.value3 = V;

	return HSV;
}

float sf_3DHisto::calculateCT(float r, float g, float b)
{
	float xyzval[3];
	RGBtoXYZ(r, g, b, xyzval);
	float temp;

	if (XYZtoCorColorTemp(xyzval, &temp) == -1)
	{
		temp = 0.0f;
	}

	return temp / 10000.0f;
}

void sf_3DHisto::drawOverlays() //draw range overlays
{
	if (overlay1_ON)
	{
		glColor4f(overlay1Colour[0], overlay1Colour[1], overlay1Colour[2], overlay1_Opacity);
		glBegin(GL_QUADS);
			glVertex3f(0.0f - overlaySize, overlay1_Value * histoheight, 0.0f - overlaySize);
			glVertex3f(height + overlaySize, overlay1_Value * histoheight, 0.0f - overlaySize);
			glVertex3f(height + overlaySize, overlay1_Value * histoheight, width + overlaySize);
			glVertex3f(0.0f - overlaySize, overlay1_Value * histoheight, width + overlaySize);
		glEnd();
	}

	if (overlay2_ON)
	{
		glColor4f(overlay2Colour[0], overlay2Colour[1], overlay2Colour[2], overlay2_Opacity);
		glBegin(GL_QUADS);
			glVertex3f(0.0f - overlaySize, overlay2_Value * histoheight, 0.0f - overlaySize);
			glVertex3f(height + overlaySize, overlay2_Value * histoheight, 0.0f - overlaySize);
			glVertex3f(height + overlaySize, overlay2_Value * histoheight, width + overlaySize);
			glVertex3f(0.0f - overlaySize, overlay2_Value * histoheight, width + overlaySize);
		glEnd();
	}
}

void sf_3DHisto::normalHisto()
{
	//build pointcloud displaylist
	input0().validate(true);
	ChannelSet channels(input0().channels());
	input0().request(channels, 0);

	glNewList(pointcloud_displaylist, GL_COMPILE);
	glPushMatrix();

	glColor4f(1.0f,0.0f,0.0f, 1.0f);
	glLineWidth(size);
	glPointSize(size);

	int x = input0().x();
	int r = input0().r();
	int y = input0().y();
	int t = input0().t();

	int count = 0;

	for( int curRow = y; curRow < t; curRow++ )
	{
		Row row ( x, r );
		Row colrow( x, r);
		input(0)->get( curRow, x, r, channels, row );

		if (histoRender == 0)
		{
			glBegin(GL_LINES);
		}
		else
		{
			glBegin(GL_POINTS);
		}

		for (int i = x; i < r; i++)
		{
			const float* rIn = row[Chan_Red] + i;
			const float* gIn = row[Chan_Green] + i;
			const float* bIn = row[Chan_Blue] + i;

			float value = getHistoValue(*rIn, *gIn, *bIn, histoDisplay);

			glColor4f(*rIn, *gIn, *bIn, 1.0f);
			if (histoRender == 0)
			{
				glVertex3f(y + count, 0.0f, i);

				if (value == 0.0f)
				{
					glVertex3f(y + count, value + 5.0f, i);
				}
				else
				{
					glVertex3f(y + count, value * histoheight, i);
				}
			}
			else
			{
				glVertex3f(y + count, value * histoheight, i);
			}

		}
		glEnd();

		count ++;

		// do some analysis here

		if (Op::aborted())
			break;
	}

	glLineWidth(1.0f);
	glPointSize(1.0f);

	glPopMatrix();
	glEndList();
}

void sf_3DHisto::customHisto() //draw specified channel
{
	//build pointcloud displaylist
	input0().validate(true);
	ChannelSet channels(input0().channels());
	input0().request(channels, 0);

	glNewList(pointcloud_displaylist, GL_COMPILE);
	glPushMatrix();

	glColor3f(1.0f,0.0f,0.0f);
	glLineWidth(size);
	glPointSize(size);

	int x = input0().x();
	int r = input0().r();
	int y = input0().y();
	int t = input0().t();

	int count = 0;
	bool channel_exists = false;
	Enumeration_KnobI * enum_interface = knob("channelSelector")->enumerationKnob();
	Channel custom_chan = getChannel(enum_interface->menu()[knob("channelSelector")->get_value(0)].c_str());

	foreach (ch, channels) //check if channel exists
	{
		if (custom_chan == ch)
		{
			channel_exists = true;
		}
	}

	if (channel_exists)
	{
		for( int curRow = y; curRow < t; curRow++ )
		{
			Row row ( x, r );
			Row colrow( x, r);
			input(0)->get( curRow, x, r, channels, row );

			if (histoRender == 0)
			{
				glBegin(GL_LINES);
			}
			else
			{
				glBegin(GL_POINTS);
			}

			for (int i = x; i < r; i++)
			{
				const float* vIn = row[custom_chan] + i;
				float value = getHistoValue(*vIn);
				glColor3f(*vIn, *vIn, *vIn);

				if (histoRender == 0)
				{
					glVertex3f(y + count, 0.0f, i);

					if (value == 0.0f)
					{
						glVertex3f(y + count, value + 5.0f, i);
					}
					else
					{
						glVertex3f(y + count, value * histoheight, i);
					}
				}
				else
				{
					glVertex3f(y + count, value * histoheight, i);
				}
			}
			glEnd();
			count ++;

			if (Op::aborted())
				break;
		}
	} //channel_exists ENDIF

	glLineWidth(1.0f);
	glPointSize(1.0f);

	glPopMatrix();
	glEndList();
}

const char* sf_3DHisto::input_label(int input, char* buffer) const
{
	if (input==0) buffer = "img";
	return buffer;
}

void sf_3DHisto::channelListGenerator()
{
	channel_list.clear();
	channel_list.push_back("");

	if (node_input( 0, OUTPUT_OP) != NULL)
	{
		ChannelMask inputchannels = input0().channels();

		ostringstream s;

		foreach (chan, inputchannels)
		{
			s << chan;
			channel_list.push_back(s.str());
			s.clear();
			s.str("");
		}
	}
}

void sf_3DHisto::knobs(Knob_Callback f)
{
	CustomKnob1(GlueKnob, f, this, "overlay1_3Dhandle");

	Enumeration_knob(f, &histoDisplay, histo_list, "histoDisplay", "render");
	Enumeration_knob(f, &channel_selection, default_channel_list, "channelSelector", "channel");
	Divider(f, "Histogram options");
	Float_knob(f, &histoheight, IRange(0, 1000), "height");
	Float_knob(f, &size, IRange(0, 100), "line/point size");
	Enumeration_knob(f, &histoRender, render_list, "render", "display");
	Bool_knob(f, &useOverlays, "useOverlays", "Overlays (on/off)");
	SetFlags(f, Knob::STARTLINE);
	BeginClosedGroup(f, "overlaysettings", "overlay settings");
		Float_knob(f, &overlaySize, IRange(1, 100), "overlayscale", "scale");

		Divider(f, "overlay 1");
		Bool_knob(f, &overlay1_ON, "overlay1_ON", "on/off");
		Bool_knob(f, &overlay1_SELECTABLE, "overlay1_SEL", "selectable");
		Float_knob(f, &overlay1_Value, IRange(0, 1), "overlay1_RANGE", "range");
		Float_knob(f, &overlay1_Opacity, IRange(0, 1), "opacity");
		Color_knob(f,overlay1Colour, "overlay1colour", "colour");

		Divider(f, "overlay 2");
		Bool_knob(f, &overlay2_ON, "overlay2_ON", "on/off");
		Bool_knob(f, &overlay2_SELECTABLE, "overlay2_SEL", "selectable");
		Float_knob(f, &overlay2_Value, IRange(0, 1), "overlay2_RANGE", "range");
		Float_knob(f, &overlay2_Opacity, IRange(0, 1), "opacity");
		Color_knob(f, overlay2Colour, "overlay2colour", "colour");
	EndGroup(f);
	Divider(f, "");
	const char* version_info = VERSION;
	Newline(f, version_info);
}

int sf_3DHisto::knob_changed(Knob* k)
{
	Enumeration_KnobI * enum_interface = knob("channelSelector")->enumerationKnob();

	if (histoDisplay == 7)
	{
		channelListGenerator();
		enum_interface->menu(channel_list);
		knob("channelSelector")->show();
	}
	else
	{
		knob("channelSelector")->hide();
	}

	knob("channelSelector")->updateWidgets();

	return 1;
}

#if DD_IMAGE_VERSION_MAJOR >= 5
static Iop* build(Node* node) { return new sf_3DHisto(node); }
#else
static Iop* build() { return new sf_3DHisto(); }
#endif
const Iop::Description sf_3DHisto::d(CLASS, 0, build);


bool sf_3DHisto::handle(ViewerContext* ctx, int index)
{
	if (index == 0) //overlay 1
	{
		overlay1_Value = ctx->y() / histoheight;
		knob("overlay1_RANGE")->set_value(overlay1_Value);
	}

	if (index == 1) //overlay 2
	{
		overlay2_Value = ctx->y() / histoheight;
		knob("overlay2_RANGE")->set_value(overlay2_Value);
	}

	return true;
}

bool sf_3DHisto::isOverlay1Selectable()
{
	return overlay1_SELECTABLE;
}

bool sf_3DHisto::isOverlay2Selectable()
{
	return overlay2_SELECTABLE;
}

bool sf_3DHisto::OverlaysActive()
{
	return useOverlays;
}

float sf_3DHisto::Overlay1Value()
{
	return overlay1_Value;
}

float sf_3DHisto::Overlay2Value()
{
	return overlay2_Value;
}

float sf_3DHisto::getHistoHeight()
{
	return histoheight;
}

int sf_3DHisto::getHeight()
{
	return height;
}

int sf_3DHisto::getWidth()
{
	return width;
}

void sf_3DHisto::build_handles(ViewerContext* ctx)
{

	// Don't draw anything unless viewer is in 3d mode:
	if (ctx->transform_mode() == VIEWER_2D)
	{
		return;
	}

	disable_flag = knob("disable")->get_value(0);

	if (node_input( 0, OUTPUT_OP) != NULL && !disable_flag)
	{
		// Cause any input iop's to draw (you may want to skip this depending on what you do)
		build_input_handles(ctx);

		// Cause any knobs to draw (we don't have any so this makes no difference):
		build_knob_handles(ctx);

		//generate OpenGL
		if (histoDisplay != 7)
		{
			normalHisto();
		}
		else
		{
			customHisto();
		}
		//

		// make it call draw_handle():
		add_draw_handle(ctx);

		// Add our volume to the bounding box, so 'f' works to include this object:
		ctx->expand_bbox(node_selected(), width, histoheight, height);
		ctx->expand_bbox(node_selected(), 0.0f, -histoheight, 0.0f);
	}
}

void sf_3DHisto::draw_handle(ViewerContext* ctx)
{
	/*	method: draw_handles
	*
	*	input: pointer to ViewerContext
	*
	*	description:
	*	This is the function that nuke calls on each node which draws the histogram in openGL.
	*/
	
	if (node_input( 0, OUTPUT_OP) != NULL && !disable_flag)
	{
		if (ctx->draw_solid())
		{
			// we are in a pass where we want to draw filled polygons
			if (!ctx->hit_detect())
			{
				glCallList(pointcloud_displaylist);
			}
		}

		if (ctx->draw_transparent())
		{
			if (useOverlays)
			{
				drawOverlays();
			}
		}
	}
}

void sf_3DHisto::_validate(bool for_real)
{
	/*	method: _validate
	*
	*	input: boolean
	*
	*	description:
	*	This function sets up the nuke node, which tells nuke how to process this particular node
	*	which channels to use, input width, input hieght.
	*/

	input(0)->validate(for_real);
	copy_info(0);

	width = info_.w();
	height = info_.h();

	set_out_channels(Mask_All);
}

void sf_3DHisto::_request(int x, int y, int r, int t, ChannelMask channels, int count)
{
	input0().request(x, y, r, t, channels, count);
}

void sf_3DHisto::engine(int y, int x, int r, ChannelMask channels, Row& out)
{
	out.get(*input(0), y, x, r, channels);
}