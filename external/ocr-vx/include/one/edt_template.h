/*
* This file is subject to the license agreement located in the file
* LICENSE_UNIVIE and cannot be distributed without it. This notice
* cannot be removed or modified.
*/
#ifndef OCR_V1__edt_template_H_GUARD
#define OCR_V1__edt_template_H_GUARD

namespace ocr_vx
{
	namespace one
	{
		struct edt_template : public guided
		{
			edt_template(guid_t guid, ocrEdt_t func, u32 paramc, u32 depc, const char* name) : guided(guid, G_edt_template), func_(func), paramc_(paramc), depc_(depc), name_(name ? name : "(unnamed)") {}
		private:
		public:
			ocrEdt_t func_;
			u32 paramc_;
			u32 depc_;
			std::string name_;
		};

	}
}
#endif
