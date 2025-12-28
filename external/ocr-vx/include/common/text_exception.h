/*
 * This file is subject to the license agreement located in the file 
 * LICENSE_UNIVIE and cannot be distributed without it. This notice
 * cannot be removed or modified.
 */

#ifndef OCR_TBB_text_exception_H_GUARD
#define OCR_TBB_text_exception_H_GUARD


struct text_exception : std::exception
{
	text_exception(const char* data) : data_(data) {}
	text_exception(const std::string& data) : data_(data) {}
	text_exception(const char* data1, const char* data2) : data_(data1)
	{
		data_ += data2;
	}
	const char* what() const throw()
	{
		return data_.c_str();
	}
	~text_exception() throw() {}
private:
	std::string data_;
};

#endif