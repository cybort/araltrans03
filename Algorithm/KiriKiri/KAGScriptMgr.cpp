#pragma warning(disable:4996)

#include "stdafx.h"
#include "Kirikiri.h"
#include "KAGScriptMgr.h"
#include "hash.hpp"

extern CKirikiriApp theApp;

int FileExists(LPCTSTR lpszName)
{
	CFileFind fileFind;
	if (!fileFind.FindFile(lpszName))
	{
		//if (DirectoryExists(lpszName)) return -1;
		return 0;
	}
	fileFind.FindNextFile();
	return fileFind.IsDirectory() ? -1 : 1;
}




CKAGScriptMgr::CKAGScriptMgr(void)
	: m_bCacheToZIP(FALSE)
	, m_bCacheToFile(FALSE)
	, m_bCacheSrc(FALSE)
	, m_pfnCallback(NULL)
	, m_pCallbackContext(NULL)
	, m_dwCurHash(0)
	, m_bLogOpened(FALSE)
{
}

CKAGScriptMgr::~CKAGScriptMgr(void)
{
	Close();
}

BOOL CKAGScriptMgr::Init()
{
	BOOL bRetVal = FALSE;
	
	Close();

	try
	{
		// 게임 경로 설정
		TCHAR wszExecPath[MAX_PATH];
		GetModuleFileName(NULL, wszExecPath, MAX_PATH);
		TCHAR* pFind = _tcsrchr(wszExecPath, _T('\\'));
		if(pFind == NULL) throw -1;
		*pFind = _T('\0');
		
		m_strGameDir = wszExecPath;

		
		// 로그파일 생성
		CString strLogFile = m_strGameDir + _T("\\ATLog.txt");
		if( m_fileLog.Open(strLogFile, CFile::modeReadWrite | CFile::modeCreate | CFile::modeNoTruncate) )
		{
			m_fileLog.Seek(0, CFile::end);
			m_bLogOpened = TRUE;
		}

		
		// ZIP 파일이 있으면 열기
		CString strATScriptFile = m_strGameDir + _T("\\ATData.zip");

		BOOL bOpened = FALSE;
		try
		{
			if( FileExists(strATScriptFile) == 1
				&& m_zip.Open(strATScriptFile, CZipArchive::zipOpen))
			{
				bRetVal = TRUE;
			}
		}
		catch(CException& ex)
		{
			// display the exception message
			ex.Delete();
		}
		catch(CException* ex)
		{
			// display the exception message
			ex->Delete();
		}
		catch(CZipException& ex	)
		{
			ex = ex;
		}
		catch(CZipException* ex	)
		{
			ex = ex;
		}

		// 열기 실패했으면 생성
		if(FALSE == bRetVal)
		{
			m_zip.Close();
			DeleteFile(strATScriptFile);
			bRetVal = m_zip.Open(strATScriptFile, CZipArchive::zipCreate);
		}

		/*
		int nCnt = m_zip.GetCount(true);
		for(int i=0; i<nCnt; i++)
		{
			CZipString strFileName = m_zip[i]->GetFileName();
			TRACE(_T("File %d : %s"), i, strFileName.c_str());
		}
		*/

	}
	catch (int nErrCode)
	{
		nErrCode = nErrCode;
	}

	return bRetVal;
}


void CKAGScriptMgr::Close()
{
	if(m_bLogOpened)
	{
		m_fileLog.Close();
		m_bLogOpened = FALSE;
	}

	m_zip.Close();
	m_bCacheSrc = FALSE;
	m_bCacheToZIP = FALSE;
	m_bCacheToFile = FALSE;
	m_dwCurHash = 0;
	m_pfnCallback = NULL;
	m_pCallbackContext = NULL;
}


BOOL CKAGScriptMgr::GetNextLine(LPWSTR& wszBuf, CString& strLine)
{
	BOOL bRetVal = FALSE;
	
	strLine = _T("");
	
	if(wszBuf && wszBuf[0])
	{
		while(wszBuf[0])
		{
			if( L'\n' == wszBuf[0] )
			{
				wszBuf ++;
				break;
			}
			else
			{
				strLine += wszBuf[0];
			}

			wszBuf++;
		}

		bRetVal = TRUE;
	}

	return bRetVal;
}


BOOL CKAGScriptMgr::GetNextToken(LPWSTR& wszLine, CString& strToken)
{
	BOOL bRetVal = FALSE;

	strToken = _T("");

	if(wszLine && wszLine[0])
	{
		strToken += wszLine[0];
		int nTagLevel = (L'[' == wszLine[0] ? 1 : 0);
		wszLine++;
		
		// '[' 태그 모드면
		if(nTagLevel)
		{
			while(*wszLine && nTagLevel > 0)
			{

				if( L'[' == *wszLine )
				{
					nTagLevel++;
				}
				else if(L']' == *wszLine)
				{
					nTagLevel--;
				}

				strToken += *wszLine;
				wszLine++;
			}

		}
		// 일반 토큰이면
		else
		{
			while(*wszLine && L'[' != *wszLine)
			{
				if(L']' == wszLine[0])
				{
					strToken += wszLine[0];
					wszLine++;
					break;
				}

				strToken += wszLine[0];
				wszLine++;
			}
		}
		


		bRetVal = TRUE;
	}

	return bRetVal;	
}


//////////////////////////////////////////////////////////////////////////
//
// <외부에서 호출되는 주 번역 함수>
//
//////////////////////////////////////////////////////////////////////////
BOOL CKAGScriptMgr::TranslateScript(LPCWSTR cwszSrc, LPWSTR wszTar)
{
	if(NULL == cwszSrc || NULL == wszTar || L'\0' == cwszSrc[0]) return FALSE;
	
	BOOL bRetVal = FALSE;

	// 원문 텍스트의 해시를 구한다.
	m_dwCurHash = MakeStringHash(cwszSrc);

	// 원문 캐싱 옵션이 있으면 저장한다.
	if(m_bCacheSrc)
	{
		// 파일명 결정
		CString strJpnFileName;
		strJpnFileName.Format(_T("Script\\JPN\\JPN_%p.txt"), m_dwCurHash);
		WriteLog(strJpnFileName + _T(" <-- Loaded..."));

		// zip 파일에 저장
		if(m_bCacheToZIP) SaveToZip(strJpnFileName, cwszSrc);

		// 파일로도 저장
		if(m_bCacheToFile) SaveToFile(strJpnFileName, cwszSrc);
	}
	
	// 저장소에서 캐싱된 데이터를 찾는다
	CString strKorFileName;
	strKorFileName.Format(_T("Script\\KOR\\KOR_%p.txt"), m_dwCurHash);
	

	// Zip에 캐싱된 데이터가 있으면
	if(m_bCacheToZIP && LoadFromZip(strKorFileName, wszTar))
	{
		TRACE(_T("[aral1] LoadFromZip('%s') OK "), (LPCTSTR)strKorFileName);
		WriteLog(strKorFileName + _T(" <-- Loaded from ZIP"));
		bRetVal = TRUE;
	}
	// 파일로 캐싱된 데이터가 있으면
	else if(m_bCacheToFile && LoadFromFile(strKorFileName, wszTar))
	{
		TRACE(_T("[aral1] LoadFromFile('%s') OK "), (LPCTSTR)strKorFileName);
		WriteLog(strKorFileName + _T(" <-- Loaded from FILE"));
		bRetVal = TRUE;
		
	}
	// 캐싱된 데이터가 없으면
	else
	{
		bRetVal = TranslateUsingTranslator(cwszSrc, wszTar);
		TRACE(_T("[aral1] TranslateUsingTranslator() result : %d"), bRetVal);

		if(bRetVal)
		{
			if(m_bCacheToZIP)
			{
				BOOL bSave = SaveToZip(strKorFileName, wszTar);
				TRACE(_T("[aral1] SaveToZip('%s') result : %d"), (LPCTSTR)strKorFileName, bSave);
				WriteLog(strKorFileName + _T(" <-- Translated and Saved to ZIP"));
			}

			if(m_bCacheToFile)
			{
				BOOL bSave = SaveToFile(strKorFileName, wszTar);
				TRACE(_T("[aral1] SaveToFile('%s') result : %d"), (LPCTSTR)strKorFileName, bSave);
				WriteLog(strKorFileName + _T(" <-- Translated and Saved to File"));
			}

		}
	}


	//TRACE(_T("[aral1] TranslateScript() end"));
	return bRetVal;
}


//////////////////////////////////////////////////////////////////////////
//
// 번역기를 사용하여 스크립트 전체를 번역한다.
//
//////////////////////////////////////////////////////////////////////////
BOOL CKAGScriptMgr::SaveToZip(LPCTSTR strFileName, LPCWSTR cwszScript)
{
	BOOL bRetVal = FALSE;

	if(strFileName && cwszScript)
	{		
		// 파일로 저장될 내용 생성
		size_t len = wcslen(cwszScript);
		wchar_t* buf = new wchar_t[len + 1];
		buf[0] = 0xFEFF;
		memcpy(&buf[1], cwszScript, len*sizeof(wchar_t));

		// 기존 ZIP 파일 삭제
		ZIP_INDEX_TYPE zip_idx = m_zip.FindFile(strFileName, CZipArchive::ffNoCaseSens);
		if( ZIP_FILE_INDEX_NOT_FOUND != zip_idx )
		{
			m_zip.RemoveFile(zip_idx);
		}

		// ZIP 파일에 추가
		CZipFileHeader templ;
		templ.SetFileName(strFileName);
		m_zip.SetFileHeaderAttr(templ, 0);
		
		// 아카이브에 새 레코드를 생성한다.
		if( m_zip.OpenNewFile(templ, CZipCompressor::levelFastest) )
		{
			// 데이터 압축 쓰기
			if( m_zip.WriteNewFile(buf, (len+1)*sizeof(wchar_t)) )
			{
				bRetVal = TRUE;
			}

			// 새 레코드 닫기
			m_zip.CloseNewFile();

		}
		
		// 퍼버 해제
		delete [] buf;
	}

	return bRetVal;
}


//////////////////////////////////////////////////////////////////////////
//
// 번역기를 사용하여 스크립트 전체를 번역한다.
//
//////////////////////////////////////////////////////////////////////////
BOOL CKAGScriptMgr::LoadFromZip(LPCTSTR strFileName, LPWSTR wszScript)
{
	BOOL bRetVal = FALSE;

	if(strFileName && wszScript)
	{
		
		ZIP_INDEX_TYPE idx = m_zip.FindFile(strFileName, CZipArchive::ffNoCaseSens);

		// 구해진 인덱스로 ZIP 안의 파일 열기
		if( ZIP_FILE_INDEX_NOT_FOUND != idx && m_zip.OpenFile(idx) )
		{
			// 파일의 크기 구하기
			DWORD uSize = (DWORD)m_zip[idx]->m_uUncomprSize;

			int buf_len = (uSize >> 1) + 2;
			wchar_t* buf = new wchar_t[buf_len];

			// 압축 해제
			char temp;
			if (m_zip.ReadFile(buf, uSize) == uSize && m_zip.ReadFile(&temp, 1) == 0)
			{
				buf[(uSize >> 1)] = L'\0';
				int nStartIdx = 0;
				if(0xFEFF == buf[0]) nStartIdx = 1;
				wcscpy(wszScript, &buf[nStartIdx]);

				bRetVal = TRUE;
			}


			// 퍼버 해제
			delete [] buf;

			// 파일 닫기			
			m_zip.CloseFile();

			// 실패라면 파일 삭제
			if(FALSE == bRetVal) m_zip.RemoveFile(idx);

		}
	}

	return bRetVal;
}


//////////////////////////////////////////////////////////////////////////
//
// 번역기를 사용하여 스크립트 전체를 번역한다.
//
//////////////////////////////////////////////////////////////////////////
BOOL CKAGScriptMgr::SaveToFile(LPCTSTR strFileName, LPCWSTR cwszScript)
{
	BOOL bRetVal = FALSE;

	if(strFileName && cwszScript)
	{		
		// 파일로 저장될 내용 생성
		size_t len = wcslen(cwszScript);
		wchar_t* buf = new wchar_t[len + 1];
		buf[0] = 0xFEFF;
		memcpy(&buf[1], cwszScript, len*sizeof(wchar_t));

		// 디렉토리 생성 (임시코드임)
		CreateDirectory(m_strGameDir + _T("\\ATData"), NULL);
		CreateDirectory(m_strGameDir + _T("\\ATData\\Script"), NULL);
		CreateDirectory(m_strGameDir + _T("\\ATData\\Script\\KOR"), NULL);
		CreateDirectory(m_strGameDir + _T("\\ATData\\Script\\JPN"), NULL);

		// 이름 보정
		CString strFullPath = m_strGameDir + _T("\\ATData\\") + strFileName;

		// 파일 생성
		CFile f;
		if( f.Open(strFullPath, CFile::modeCreate | CFile::modeWrite) )
		{
			f.Write(buf, (len+1)*sizeof(wchar_t));
			f.Close();
			
			bRetVal = TRUE;
		}

		// 퍼버 해제
		delete [] buf;
	}

	return bRetVal;
}


//////////////////////////////////////////////////////////////////////////
//
// 번역기를 사용하여 스크립트 전체를 번역한다.
//
//////////////////////////////////////////////////////////////////////////
BOOL CKAGScriptMgr::LoadFromFile(LPCTSTR strFileName, LPWSTR wszScript)
{
	BOOL bRetVal = FALSE;

	if(strFileName && wszScript)
	{
		// 이름 보정
		CString strFullPath = m_strGameDir + _T("\\ATData\\") + strFileName;

		// 파일 열기
		CFile f;
		if( f.Open(strFullPath, CFile::modeRead) )
		{
			UINT nLen = (UINT)f.GetLength();
			
			wchar_t* buf = new wchar_t[(nLen>>1) + 1];

			if( f.Read(buf, nLen) == nLen )
			{
				buf[(nLen>>1)] = L'\0';

				int nStartIdx = 0;
				if(	buf[0] == 0xFEFF ) nStartIdx = 1;
				wcscpy(wszScript, &buf[nStartIdx]);

				bRetVal = TRUE;

			}

			f.Close();

			// 퍼버 해제
			delete [] buf;
		}

	}

	return bRetVal;
}



void DeletePath(CString strPath)
{
	CFileFind finder;
	BOOL bContinue = TRUE;

	if(strPath.Right(1) != _T("\\"))
		strPath += _T("\\");

	strPath += _T("*.*");
	bContinue = finder.FindFile(strPath);
	while(bContinue)
	{
		bContinue = finder.FindNextFile();
		if(finder.IsDots()) // Ignore this item.
		{
			continue;
		}
		else if(finder.IsDirectory()) // Delete all sub item.
		{
			DeletePath(finder.GetFilePath());
			::RemoveDirectory((LPCTSTR)finder.GetFilePath());
		}
		else // Delete file.
		{
			::DeleteFile((LPCTSTR)finder.GetFilePath());
		}
	}


	finder.Close();


	strPath = strPath.Left(strPath.ReverseFind('\\'));   // "c:\\a\\"  -->  "c:\\a" 이역활
	::RemoveDirectory((LPCTSTR)strPath);


}



//////////////////////////////////////////////////////////////////////////
//
// 캐시 초기화
//
//////////////////////////////////////////////////////////////////////////
BOOL CKAGScriptMgr::ClearCache()
{
	BOOL bRetVal = FALSE;

	// 파일 삭제
	DeletePath(m_strGameDir + _T("\\ATData\\Script"));

	// ZIP파일 삭제
	CString strATScriptFile = m_strGameDir + _T("\\ATData.zip");
	m_zip.Close();
	DeleteFile(strATScriptFile);

	// ZIP 파일 새로 생성
	bRetVal = m_zip.Open(strATScriptFile, CZipArchive::zipCreate);

	return bRetVal;
}



//////////////////////////////////////////////////////////////////////////
//
// 번역기를 사용하여 스크립트 전체를 번역한다.
// (완전 발로 코딩했음;; 머냐 이게.. )
//
//////////////////////////////////////////////////////////////////////////
BOOL CKAGScriptMgr::TranslateUsingTranslator(LPCWSTR cwszSrc, LPWSTR wszTar)
{
	BOOL bRetVal = FALSE;

	LPWSTR wszSrc = (LPWSTR)cwszSrc;
	int nTotalLines = GetTotalLines(cwszSrc);
	int nCurLine = 0;
	
	// 콜백함수로 시작 통지
	if(m_pfnCallback)
	{
		m_pfnCallback(m_pCallbackContext, 0, nTotalLines);
	}

	CString strLine = L"";
	CString strSkipTillThisString = L"";
	
	try
	{
		while( GetNextLine(wszSrc, strLine) )
		{
			//log.Write( (LPCWSTR)(_T("[IN ] ") + strLine + _T("\r\n")), (strLine.GetLength() + 8)*2 );

			CString strFilterLine = strLine;
			strFilterLine.Remove(_T('\t'));

			// 스킵 상태인 경우
			if(!strSkipTillThisString.IsEmpty())
			{
				strLine = TranslateOnlyTextToken(strLine);
				size_t len = wcslen((LPCWSTR)strLine);
				memcpy(wszTar, (LPCWSTR)strLine, len*sizeof(wchar_t));
				wszTar += len;

				if(strLine.Find(strSkipTillThisString) >= 0) strSkipTillThisString = L"";
			}
			// 일반 상태인 경우
			// 일반이 아니라 선언문 계열 아닌가;;
			//  
			else if(L';' == strFilterLine[0] || L'@' == strFilterLine[0] || L'*' == strFilterLine[0])
			{
				// 수평선은 몇 마일? 에서 * 일때도 TranslateOnlyTextToken 을 거치게 한다..
				//if(L'@' == strFilterLine[0]) strLine = TranslateOnlyTextToken(strLine);
				if(L';' != strFilterLine[0]) strLine = TranslateOnlyTextToken(strLine);
				size_t len = wcslen((LPCWSTR)strLine);
				memcpy(wszTar, (LPCWSTR)strLine, len*sizeof(wchar_t));
				wszTar += len;

				if(strFilterLine.Find(L"@iscript") >= 0 ) strSkipTillThisString = L"@endscript";
			}
			// 토큰화가 필요한 라인이라면
			else if (!strLine.IsEmpty())
			{
				CString strToken = L"";
				LPWSTR wszLine = (LPWSTR)(LPCWSTR)strLine;
				while ( GetNextToken(wszLine, strToken) )
				{
					int nDotIdx = GetHelperTextCommaIdx(strToken);

					// 일반 제어 토큰이라면
					if( strToken == L"\t" || (strToken[0] == L'[' && nDotIdx < 0) )
					{
						if(L'[' == strToken[0])
						{
							// case "[:...]"
							if(L':' == strToken[1])
							{
								CString strTextPart = strToken.Mid(2, strToken.GetLength()-3);
								wchar_t wszTransTextPart[256];
								if(TranslateUnicodeText(strTextPart, wszTransTextPart))
								{
									strToken.Format(L"[:%s]", wszTransTextPart);
								}
							}
							// case "[seladd ~" 선택지!
							else if(strToken.Mid(1,6).Compare(L"seladd")==0)
							{
								//기본 구조
								//[seladd text=たまには寿司もいいかな	storage="06_sak_01_b_02_06.ks"]
								//
								//text가 따옴표가 있을수도 있고, 없을 수도 있다[...]
								//그런데 한글은 띄어쓰기가 있으니 무조건 붙여야된다
								//
								//적당히 알아서 해야겠지 =ㅅ=;
								//Hide_D君

								bool bIsQuote = false;
								int nTextStart = strToken.Find(L"text",7);
								if (nTextStart > 0)
								{
									nTextStart = strToken.Find(L'=',nTextStart+1) + 1;

									while(strToken[nTextStart] == L'\t' || strToken[nTextStart] == L' ')
										nTextStart++;

									int nTextEnd= nTextStart+1;

									if(strToken[nTextStart]==L'\"')
									{
										//따옴표가 있는 경우
										bIsQuote = true;

										nTextEnd = nTextStart + 1;

										//따옴표 끝까지 달린다. 두개 붙은건 빼고,
										while(1)
										{
											nTextEnd = strToken.Find(L'\"',nTextEnd);

											if(strToken[nTextEnd+1]!=L'\"')
												break;

											nTextEnd+=2;
										}

										nTextEnd--;
										//따옴표가 없는 부분만 선택되었다
									}
									else
									{
										//따옴표가 없는 경우
										bIsQuote = false;

										//공백이나 탭까지 달린다.
										while(strToken[nTextEnd]!=L' ' && strToken[nTextEnd]!=L'\t')
										{
											nTextEnd++;
										}

									}//따옴표 조건 체크 끝

									CString strTextPart = strToken.Mid(nTextStart,nTextEnd-nTextStart+1);
									wchar_t wszTransTextPart[256];
									if(TranslateUnicodeText(strTextPart, wszTransTextPart))
									{
										if(bIsQuote)
										{
											strToken = strToken.Left(nTextStart) + wszTransTextPart + strToken.Mid(nTextEnd+1);
										}
										else
										{
											strToken = strToken.Left(nTextStart) + L'\"' + wszTransTextPart + L'\"' + strToken.Mid(nTextEnd+1);
										}
									}
								}
								else
								{
									strToken = TranslateOnlyTextToken(strToken);
								}

							}//seladd끝
							else
							{
								strToken = TranslateOnlyTextToken(strToken);
							}
						}
						size_t len = wcslen((LPCWSTR)strToken);
						memcpy(wszTar, (LPCWSTR)strToken, len*sizeof(wchar_t));
						wszTar += len;

						if(strToken == L"[iscript]")
						{
							len = wcslen(wszLine);
							memcpy(wszTar, wszLine, len*sizeof(wchar_t));
							wszTar += len;

							if(strLine.Find(L"[endscript]") < 0) strSkipTillThisString = L"[endscript]";
							break;
						}
					}

					// 텍스트이거나 윗 첨자이면
					else
					{
						CString strTextJpn = _T("");

						while(!strToken.IsEmpty())
						{

							if(nDotIdx > 0)
							{
								strTextJpn += strToken.Mid(1, nDotIdx-1);
								//strHelperText = strToken.Mid(nDotIdx+1, len-nDotIdx-2);								
							}
							else
							{
								strTextJpn += strToken;
							}

							strToken = _T("");

							LPWSTR tmpLine = wszLine;

							// 임의로 한개의 토큰을 얻어보고 연결시켜 해석해야 한다면
							if( GetNextToken(tmpLine, strToken) && !strToken.IsEmpty() )
							{
								nDotIdx = GetHelperTextCommaIdx(strToken);

								// 윗첨자 문자열도 아니고 일반 텍스트도 아니면 빠져나감
								if(nDotIdx < 0 && L'[' == strToken[0]) 
								{
									strToken = _T("");
									break;
								}

								wszLine = tmpLine;
							}

						}

						// 번역
						wchar_t tmpKorText[1024];
						ZeroMemory(tmpKorText, 1024*sizeof(wchar_t));
						TranslateUnicodeText(strTextJpn, tmpKorText);

						int nJpnLen = strTextJpn.GetLength();

						/*
						if( nJpnLen > 2
							&& 0x3010 == strTextJpn[0]
							&& 0x3011 == strTextJpn[nJpnLen-1] )
						*/
						//저 위의 경우에는 이름과 대사가 한줄에 있을경우 확인이 불가능하다.
						//예 : 【妙】「ちゃんと生きてるみたいだね……」
						//따라서 그냥 Find로 변경
						// by Hide_D君

						//속도 약~간 향상

						int nJpnNameStart = -1;
						int nJpnNameEnd	  = -1;

						if(0x3010 == strTextJpn[0])
							nJpnNameStart = 0;

						if(nJpnNameStart != -1)
							nJpnNameEnd = strTextJpn.Find(0x3011,1);

						// 이름 시작/끝점 찾기 일본어

						if( nJpnLen > 2
							&&	nJpnNameStart != -1
							&&  nJpnNameEnd != -1)
						{
							CString strTextKor = tmpKorText;

							int nKorNameStart = strTextKor.Find(0x3010);
							int nKorNameEnd = strTextKor.Find(0x3011,nKorNameStart+1);
							// 이름 시작,끝점 찾기 한국어

							int nJpnSlash = CString(strTextJpn.Left(nJpnNameEnd)).Find(_T('/'),1);
							int nKorSlash = CString(strTextKor.Left(nKorNameEnd)).Find(_T('/'),1);
							
							// 슬래시가 이미 포함된 경우
							if(nJpnSlash > 0 && nKorSlash > 0)
							{
								strTextKor = strTextJpn.Left(nJpnSlash) + strTextKor.Mid(nKorSlash);
							}
							// 슬래시가 없으면 이름을 재구성한다.
							else
							{
								strTextKor = strTextJpn.Left(nJpnNameEnd) + L"/" + strTextKor.Mid(nKorNameStart+1);	
								//요기도 수정 =ㅅ=
							}

							size_t len = strTextKor.GetLength();
							memcpy(wszTar, (LPCWSTR)strTextKor, len*sizeof(wchar_t));
							wszTar += len;
						}
						else
						{
							size_t len = wcslen(tmpKorText);
							memcpy(wszTar, (LPCWSTR)tmpKorText, len*sizeof(wchar_t));
							wszTar += len;
						}

					}

				}// end of while (Token)

			}// end of else

			*wszTar = L'\r'; wszTar++;
			*wszTar = L'\n'; wszTar++;
			*wszTar = L'\0';

			nCurLine++;

			// 콜백으로 진행 상황 통지
			if(m_pfnCallback)
			{
				int nContinue = m_pfnCallback(m_pCallbackContext, nCurLine, nTotalLines);
				if(0 == nContinue) throw -1;
			}
		}// end of while (Line)

		bRetVal = TRUE;

	}
	catch (int nErrCode)
	{
		nErrCode = nErrCode;
		bRetVal = FALSE;
	}


	return bRetVal;
}

CString CKAGScriptMgr::TranslateOnlyTextToken( LPCWSTR cwszLine )
{
	CString strRetVal = _T("");
	CString strTextToken = _T("");

	int i = 0;
	//BOOL bTextMode = FALSE;
	wchar_t chTextMode = L'\0';
	while(cwszLine[i])
	{
		// 따옴표를 만났다면
		if( ( L'\0' == chTextMode && (L'\"' == cwszLine[i] || L'\'' == cwszLine[i]) )
			|| ( L'\0' != chTextMode && chTextMode == cwszLine[i]  ) )
		{
			// 텍스트 수집 모드 였다면
			if(chTextMode)
			{
				int nStrLen = strTextToken.GetLength();
				wchar_t tmpBuf[1024];
				// "txt=", "text="
				if( i > nStrLen+9 
					&& ( 
						wcsncmp(L" txt=", &cwszLine[i-5-1-nStrLen], 5) == 0 
						|| wcsncmp(L" txt = ", &cwszLine[i-7-1-nStrLen], 7) == 0 
						|| wcsncmp(L" text=", &cwszLine[i-6-1-nStrLen], 6) == 0 
						|| wcsncmp(L" text = ", &cwszLine[i-8-1-nStrLen], 8) == 0 
					)
					&& TranslateUnicodeText(strTextToken, tmpBuf) )
				{
					strRetVal += tmpBuf;
				}
				else
				{
					strRetVal += strTextToken;
				}

				// 변수들 초기화
				chTextMode = '\0';
				strTextToken = _T("");
			}
			// 일반 모드 였다면
			else
			{
				chTextMode = cwszLine[i];
			}

			strRetVal += cwszLine[i];
			
		}
		// 따옴표가 아니면
		else
		{
			if(chTextMode) strTextToken += cwszLine[i];
			else strRetVal += cwszLine[i];

		}

		// 인덱스 증가
		i++;
	}

	// 모으고 있던 텍스트 토큰이 있다면 붙여준다.
	if(chTextMode)
	{
		//strRetVal += L'\"';
		strRetVal += strTextToken;
	}

	return strRetVal;
}

//////////////////////////////////////////////////////////////////////////
//
// 번역기를 사용하여 일본어 문장을 번역한다.
//
//////////////////////////////////////////////////////////////////////////
BOOL CKAGScriptMgr::TranslateUnicodeText( LPCWSTR cwszSrc, LPWSTR wszTar )
{
	BOOL bRetVal = FALSE;

	if(NULL == cwszSrc || NULL == wszTar || wcslen(cwszSrc) > 1024) return FALSE;
	
	char tmpJpn[2048];
	char tmpKor[2048];
	ZeroMemory(tmpJpn, 2048);
	ZeroMemory(tmpKor, 2048);

	int nSrcLen = MyWideCharToMultiByte(932, 0, cwszSrc, -1, tmpJpn, 2048, NULL, NULL);

	if(theApp.m_sATCTNR3.procTranslateUsingCtx)
	{
		bRetVal = theApp.m_sATCTNR3.procTranslateUsingCtx(L"KiriKiriContext", tmpJpn, nSrcLen, tmpKor, 2048);

		if(TRUE == bRetVal)
		{
			MyMultiByteToWideChar(949, 0, tmpKor, -1, wszTar, 1024);
		}
	}
	
	if (!bRetVal)
	{
		int nSize = lstrlenW(cwszSrc) + 1;
		CopyMemory(wszTar, cwszSrc, nSize * 2);
	}

	return bRetVal;
}

int CKAGScriptMgr::GetHelperTextCommaIdx( LPCWSTR cwszToken )
{
	int nDotIdx = -1;
	
	if(cwszToken && cwszToken[0] == L'[')
	{
		size_t len = wcslen(cwszToken);

		// 윗첨자인지 검사
		for(int k=1; k<(int)len-1; k++)
		{
			if(L',' == cwszToken[k])
			{
				if(nDotIdx > 0)
				{
					nDotIdx = -1;
					break;
				}
				else
				{
					nDotIdx = k;
				}
			}
			else if(cwszToken[k] < (wchar_t)0x80)
			{
				nDotIdx = -1;
				break;									
			}
		}

	}
	
	return nDotIdx;

}

void  CKAGScriptMgr::SetProgressCallback(void* pContext, PROC_TransProgress pfnCallback)
{
	m_pCallbackContext = pContext;
	m_pfnCallback = pfnCallback;
}

int CKAGScriptMgr::GetTotalLines( LPCWSTR cwszText )
{
	int nRetVal = 0;
	
	if(cwszText && cwszText[0])
	{
		nRetVal = 1;

		while(cwszText[0])
		{
			if( L'\n' == cwszText[0] )
			{
				nRetVal++;
			}

			cwszText++;
		}
	}

	return nRetVal;
}

UINT CKAGScriptMgr::GetCurrentScriptHash()
{
	
	return m_dwCurHash;
}



void CKAGScriptMgr::WriteLog( LPCTSTR cszLine )
{
	CTime t = CTime::GetCurrentTime();
	m_fileLog.WriteString(t.Format(_T("[%Y-%m-%d %H:%M:%S] ")) );
	m_fileLog.WriteString(cszLine);
	m_fileLog.WriteString(_T("\r\n"));
}


BOOL CKAGScriptMgr2:: TranslateUsingTranslator(LPCWSTR cwszSrc, LPWSTR wszTar)
{
	BOOL bRetVal = FALSE;
	switch(m_nMode)
	{
		case NORMAL:
			bRetVal = CKAGScriptMgr::TranslateUsingTranslator(cwszSrc, wszTar);
			break;

		case SILVERHAWK:
			bRetVal = TranslateSilverHawkScript(cwszSrc, wszTar);
			break;
		
		case NOTHING:
		default:;
			// 무시 - 항상 번역불가 (FALSE)를 돌려준다.
			
	}

	return bRetVal;
}

BOOL CKAGScriptMgr2::TranslateSilverHawkScript(LPCWSTR cwszSrc, LPWSTR wszTar)
{
	BOOL bRetVal = FALSE;

	LPWSTR wszSrc = (LPWSTR)cwszSrc;
	int nTotalLines = GetTotalLines(cwszSrc);
	int nCurLine = 0;

	CString strLine = L"";

	// 콜백함수로 시작 통지
	if(m_pfnCallback)
	{
		m_pfnCallback(m_pCallbackContext, 0, nTotalLines);
	}


	try
	{
		WCHAR wch, wszKorText[1024];
		while ( GetNextLine(wszSrc, strLine) )
		{
			wch = strLine[0];
			size_t nLen;
			if (wch == L'　')
			{
				// 모노로그
				ZeroMemory(wszKorText, 1024 * sizeof(WCHAR));
				TranslateUnicodeText(strLine.Mid(1).GetString(), wszKorText);
				nLen = wcslen(wszKorText);
				wsprintfW(wszTar, L"　%s", wszKorText);
				wszTar += nLen+1;
			}
			else if (wch == L'【')
			{
				// 대사
				CString strName;
				int idxDialog = strLine.Find(L'】') + 1;
				strName = strLine.Mid(1, idxDialog - 2);

				TranslateUnicodeText(strName, wszKorText);
				strName = wszKorText;

				TranslateUnicodeText(strLine.Mid(idxDialog), wszKorText);
				nLen = wcslen(wszKorText);
				wsprintfW(wszTar, L"【%s】%s", strName.GetString(), wszKorText);
				wszTar += 1 + strName.GetLength() + 1 + nLen;
			}
			else if ( strLine.Find(L"選択肢登録") >= 0 )
			{
				// 선택지 스크립트
				int idxSelStart, idxSelEnd;

				idxSelStart = strLine.Find(L'\"', 5) + 1;
				idxSelEnd = strLine.Find(L'\"', idxSelStart+1) -1;

				if (idxSelStart >= 0 && idxSelEnd >= 0)
				{
					CString strSel = strLine.Mid(idxSelStart, idxSelEnd - idxSelStart + 1);
					TranslateUnicodeText(strSel, wszKorText);

					strLine.Replace(strSel, wszKorText);
				}
				wsprintfW(wszTar, L"%s", strLine.GetString());
				wszTar+=strLine.GetLength();
			}
			else
			{
				// 일반 스크립트 명령, 레이블, 주석 등등
				wsprintfW(wszTar, L"%s", strLine.GetString());
				wszTar+=strLine.GetLength();
			}

			*wszTar = L'\r'; wszTar++;
			*wszTar = L'\n'; wszTar++;
			*wszTar = L'\0';
			
			nCurLine++;

			// 콜백으로 진행 상황 통지
			if(m_pfnCallback)
			{
				int nContinue = m_pfnCallback(m_pCallbackContext, nCurLine, nTotalLines);
				if(0 == nContinue) throw -1;
			}


		}	// end of while (Line)

		bRetVal = TRUE;

	}
	catch (int nErrCode)
	{
		nErrCode = nErrCode;
		bRetVal = FALSE;
	}
	return bRetVal;
}