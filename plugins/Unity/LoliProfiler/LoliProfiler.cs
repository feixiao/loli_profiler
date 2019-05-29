using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

public class LoliProfiler : MonoBehaviour {

#if UNITY_ANDROID && !UNITY_EDITOR
    [DllImport("loli")]
    private static extern int loliHook();
    [DllImport("loli", CallingConvention = CallingConvention.Cdecl)]
    private static extern int loliDump(bool append, string path);
#else
    private static int loliHook() { return 0; }
    private static int loliDump(bool append, string path) { return 0; }
#endif

    private string loliFilePath;

    private void Awake()
    {
        loliFilePath = Path.Combine(Application.persistentDataPath, "loli.csv");
        Debug.Log("loliHookStatus: " + loliHook());
    }
    
    float Timer = 0.0f;
    private void Update()
    {
        Timer += Time.deltaTime;
        if (Timer >= 10.0f)
        {
            loliDump(false, loliFilePath);
            Timer -= 10.0f;
        }
    }
}
