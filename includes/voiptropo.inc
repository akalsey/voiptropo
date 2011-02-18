<?php
// $id$

/**
 * @file
 * Implementation of tropo-related functionality
 */

/**
 * Internal functions
 */

/*
 * Process incoming requests and responses from tropo
 *
 * @param string $tropo_action set to the action to be executed. 
 *   It can assume any of the following values:
 *   - process_conference, to handle responses to conference call requests
 *   - process_inbound_calls, to handle incoming calls
 *   - process_outbound_calls, to handle the response to outgoing call requests
 *   - process_dial, to handle the response to the "dial" script command
 *   - process_get_input, to handle responses to "get input" script commands
 *   - process_hangup, to handle call termination
 *   - process_record, process_record_mp3 and process_empty_recording, to handle
 *     responses to 'record' script commands
 *   - process_send_sms, to handle responses to 'send sms' script commands
 *   - process_sms_callback, to handle final responses to 'send sms'.
 *
 * @param string $call_nid with the nid of the node associated with the call
 *
 * @return void
 */
function _voiptropo_call_handler($tropo_action='process_inbound_calls', $call_nid=NULL) {

  try {
    $session = new Session();
	$json = json_decode(@file_get_contents('php://input')); 
	$msg = "in voiptropo_call_handler() with tropo_action: $tropo_action, call_nid: $call_nid and Session ".print_r($json, TRUE);
	$vars = array();
	watchdog('voiptropo', $msg, $vars, WATCHDOG_INFO);
	//Test
	//$tropo=new Tropo();
	
	watchdog('voiptropo', 'JSON '.print_r($json->{"session"}->{"from"}->{"name"}, TRUE), $vars, WATCHDOG_INFO);
	
	
	$incoming_sid = $session->getAccountID();
  }
  catch (TropoException $e) {
    //var_dump($e);
    watchdog('voiptropo', 'error in _voip_tropo_call_handler, Tropo reported: '.$e->getMessage(), array(), WATCHDOG_ERROR);
	return FALSE;  // TODO: should we return an error response to the server?
  }

  // validate $_REQUEST['AccountSid'] to avoid hack attempts
  $server = VoipServer::getServer('tropo');
  $server_config = $server->getConfig();
  
  /*$account_username = $server_config['voiptropo_account_username'];
  $incoming_sid = $_REQUEST['AccountSid'];
  if ($account_sid != $incoming_sid) {
     $msg = '_voip_tropo_call_handler called with invalid AccountSid: @sid';
     $params = array('@sid' => $incoming_sid);
     watchdog('voiptropo', $msg, $params, WATCHDOG_WARNING);
     return FALSE;  // TODO: should we return an error response to the server?
  }
*/
  // handle incoming call requests
  if($tropo_action == 'process_inbound_calls') {

    // make sure incoming calls are allowed in the system
    // NOTE: Since incoming calls always arrive with anonymous user, make sure
    //   that anonymous users have permission to make inbound calls
    $allowed = user_access('make inbound calls');
    if(!$allowed) {
      // log the issue
      $msg = 'Unauthorized call received with Account ID: @sid';
      $params = array('@sid' => $incoming_sid);
      watchdog('voiptropo', $msg, $params, WATCHDOG_WARNING);

      // reject the call
	  $reject = new Reject();
      return;  // NOTE: this function MUST not return any value.  If it does,
               // the output to tropo will get messed up with Drupal hmtl...
    }
    
    $call = new VoipCall();
    $call_sid = $json->{"session"}->{"callId"}; // unique identifier of the call, 34 char
    $call->setCallId($call_sid);
    $call->setSource('tropo');
    $dir_tmp = VoipCall::INBOUND;
    $call->setDirection($dir_tmp);
    $call->setAnsweredBy(VoipCall::ANSWERED_DRUPAL);
    $call->setCallStatus(VoipCall::RINGING);
    $call->setStartTime(time());

    // create a new node for the call
    $call->save();

    // get the nid of the call node
    $call_nid = $call->getNodeId();
  }
  else {
    // handle ongoing calls

    // 'process_hangup' for incoming calls does not come with a $call_nid
    if($tropo_action == 'process_hangup') {
      if(!$call_nid) {
        $call_id = $_REQUEST['CallSid'];
        $call_nid = VoipCall::getNidFromCallId($call_id);
      }
    }

    // load the variables already associated with the call, if any
    $call = VoipCall::load($call_nid);

    // handle first response to an outbound call
    if($tropo_action == 'process_outbound_calls') {
      // store tropo call id for all outbound calls
      $call_id = $json->{"session"}->{"callId"};
      $call->setCallId($call_id);
      //TODO:
	  //$answered_by = $_REQUEST['AnsweredBy'];
      if($answered_by) {
        $tmp = ($answered_by == 'human') ? VoipCall::ANSWERED_HUMAN
                                         : VoipCall::ANSWERED_MACHINE; 
        $call->setAnsweredBy($tmp);
      }
    }
  }

  // update common call variables
  if(!$call->getCallerNumber())
    //TODO:
    $call->setCallerNumber($_REQUEST['Caller']);
  if(!$call->getCallerName())
    $call->setCallerName($json->{"session"}->{"from"}->{"name"});
  if(!$call->getDestNumber())
    //TODO: Check if this is always correct? In case of Skype it gives number.
    $call->setDestNumber($json->{"session"}->{"to"}->{"name"});
  //TODO:
  $tropo_call_status = $_REQUEST['CallStatus']; 

  // set start time for the call
  if($tropo_call_status == 'in-progress'){
    $start_time = $call->getStartTime();
    if(!$start_time) {
      // start counting the call duration
      $call->setStartTime();
    }
  }

watchdog('voiptropo', "tropo status: $tropo_call_status, call status: " . $call->getCallStatus());

  // update call status
  // NOTE: sms callbacks do not have a call status associated with them
  if ($tropo_action == 'process_sms_callback'){
    //TODO: Should we move sms callback to another menu callback?
    $call->SetCallStatus(VoipCall::IN_PROGRESS);
  }
  else{
    $call = voiptropo_update_call_status($call, $tropo_call_status);
  }

  //TODO: Is this possible with Tropo? update script variables with call location, if available
  /*if(isset($_REQUEST['FromCity'])){
    $call_location = array();
    $call_location['caller_city'] = $_REQUEST['FromCity'];
    $call_location['caller_state'] = $_REQUEST['FromState'];
    $call_location['caller_zip'] = $_REQUEST['FromZip'];
    $call_location['caller_country'] = $_REQUEST['FromCountry'];
    $call_location['called_city'] = $_REQUEST['ToCity'];
    $call_location['called_state'] = $_REQUEST['ToState'];
    $call_location['called_zip'] = $_REQUEST['ToZip'];
    $call_location['called_country'] = $_REQUEST['ToCountry'];
    $script = $call->getScript();
    $script->setVar('call_location', $call_location);
    $call->setScript($script);
  }*/

  $is_hangup = $call->isHangup();

watchdog('voiptropo', "tropo status: $tropo_call_status, updated call status: " . $call->getCallStatus() . ", is_hangup: $is_hangup");

  // process responses to requests previously-issued by the call script
  if(!$is_hangup) {
    switch($tropo_action) {
      default:
        $msg = 'Invalid tropo_action: @action';
        $params = array('action' => $tropo_action);
        $call->report_error('voiptropo', $msg, $params);
        return FALSE;
        break;
      case 'process_inbound_calls':
      case 'process_outbound_calls':
      case 'process_hangup':
      case 'process_conference':
        // nothing special to be done here. the above commands have already
        // been processed in the previous lines
        break;
      case 'process_dial':
        $call = _voiptropo_process_dial_response($call, $_REQUEST);
        break;
      case 'process_get_input':
        $script = $call->getScript();
        $script->setVar('input_digits', $_REQUEST['Digits']);
        $call->setScript($script);
        break;
      case 'process_record':
      case 'process_record_mp3':
        $suffix = ($tropo_action == 'process_record')? '.wav' : '.mp3';
        $script = $call->getScript();
        $script->setVar('recording_url', $_REQUEST['RecordingUrl'] . $suffix);
        $script->setVar('recording_duration', $_REQUEST['RecordingDuration']);
        $script->setVar('recording_digits', $_REQUEST['Digits']);
        $call->setScript($script);
        break;
      case 'process_empty_recording':
        $script = $call->getScript();
        $script->setVar('recording_url', '');
        $script->setVar('recording_duration', 0);
        $script->setVar('recording_digits', NULL);
        $call->setScript($script);
        break;

      case 'process_send_sms':
      case 'process_sms_callback':
        $call = _voiptropo_process_sms_response($call, $_REQUEST);
        break;
    }
  }

  if($is_hangup) {
    // determine call duration
    $end_time = $call->setEndTime();
    $start_time = $call->getStartTime();
    if($start_time) {
      $duration  = $end_time - $start_time;
    }
    else {
     // get call duration from tropo
     $duration = isset($_REQUEST['CallDuration'])?$_REQUEST['CallDuration']:NULL;
    }
    $call->setDuration($duration);
  }

  // update the voipcall node with the new values
  $call = $call->save();

  // process the call script
  $rc = _voiptropo_script_handler($call);

  return;  // NOTE: this function MUST NOT return any value.  If it does, the
           //       output to tropo will get messed up with Drupal hmtl...
}

/**
 * Process voipcall scripts
 *
 * @param $call, VoipCall instance with the call information
 *
 * @return boolean.  TRUE on success or FALSE on failure.  Check voip_error() for processing failure.
 */
function _voiptropo_script_handler($call) {
$msg = 'Entering script_handler with voipcall: ' . print_r($call,true);
watchdog('voiptropo', $msg, array(), WATCHDOG_INFO);

  $rc = TRUE;

  // get the script object associated with the call
  $script = $call->getScript();

  // check if call is in a hangup state
  $hangup = $call->isHangup();
  if($hangup) {
    $hangup_script_name = $call->getHangupScriptName();
    // Stop processing if no hangup script defined
    if(!$hangup_script_name) {
      return TRUE;
    }
    else {
      // load hangup script commands
      $rc = $script->runGosub($hangup_script_name);
      if($script->isError()){
        $msg = $script->getErrorMessage();
        $params = array();
        $call->report_error('voiptropo', $msg, $params);
        return FALSE;
      }
    }
  }

  // process script commands
  $processing = TRUE;
  $response = '';
  $tropo = new Tropo();

  while($processing) {
   
    $cmd = $script->getNextCommand();
	
	//Set script language
	$language= $script->getLanguage();
    $voice = $script->getVoice();
    $tropo->setVoice($voice);
    if(!$cmd) {
      // no more commands to be processed
      // send a final hangup request 
      //$response .= '<Hangup/>';
	  $tropo->hangup();
      $msg = 'Script @name ended without a "hangup" command.';
      $params = array('@name' => $script->getName());
      watchdog('voiptropo', $msg, $params, WATCHDOG_WARNING);
      $call->setHangupReason(VoipCall::HANGUP_SCRIPT_END);
      $processing = FALSE;
    }

    // process current command
    else {

      $cmd_id = $cmd->getId();
   
      switch($cmd_id) {
        default:
          $msg = "Invalid command id '@cmd_id' in script '@script_name': @script";
          $params = array('@cmd_id' => $cmd_id, '@script_name' => $script->getName(), '@script' => print_r($script,true));
          $call->report_error('voiptropo', $msg, $params);
          $rc = FALSE;
          $processing = FALSE;
          break;

        case 'VoipCmdDial';
          $call->setCallStatus(VoipCall::IN_PROGRESS);
          $menu_path = 'voip/tropo/callhandler';
          $call_nid = $call->getNodeId();
          $response_url = $menu_path . '/process_dial/' . $call_nid ;
          $action = url($response_url, array('absolute' => TRUE));
          $method = 'POST';
          $number = $script->evalString($cmd->getParam('number'));
          
          $timeout = $script->evalString($cmd->getParam('timeout', 30));
          $hangup_on_star =  $script->evalString($cmd->getParam('hangup_on_star', false));
          $hangup_on_star =  $hangup_on_star? 'true':'false';
          $time_limit = $script->evalString($cmd->getParam('time_limit', '14400'));
          $caller_number = $script->evalString($script->getVar('caller_number'));
          $caller_id = $script->evalString($cmd->getParam('caller_id', $caller_number)); 
          /*$response .= '<Dial action="' . $action
                            . '" method="' . $method
                            . '" timeout="' . $timeout
                            . '" hangupOnStar="' . $hangup_on_star
                            . '" timeLimit="' . $time_limit
                            . '" callerId="' . $caller_id
                            . '" >';
          $response .= '<Number>' . $number . '</Number>';
          $response .= '</Dial>';*/
		  //TODO: What command for timeLimit, hangupOnStar?
		  $params = array(
		    'timeout' => $timeout,
		    'from' => $caller_number,
		  );
		  $tropo->call($number, $params);
          $rc = TRUE;
          $processing = FALSE;
          break;

        case 'VoipCmdGetInput':
          $call->setCallStatus(VoipCall::IN_PROGRESS);
          $menu_path = 'voip/tropo/callhandler';
          $call_nid = $call->getNodeId();
          $response_url = $menu_path . '/process_get_input/' . $call_nid;
          $action = url($response_url, array('absolute' => TRUE));
          $method = 'POST';
          $timeout = $script->evalString($cmd->getParam('timeout', 5)); 
          //$end_key = $script->evalString($cmd->getParam('end_key', '')); 
          $num_digits = $script->evalString($cmd->getParam('num_digits')); 
		  if($num_digits<=1){
		    $choices="[1 DIGIT]";
		  }
		  else{
		    $choices="[$num_digits DIGITS]"
		  }
          $text = $script->evalString($cmd->getParam('text')); 
          //$url = $script->evalString($cmd->getParam('url'));
          /*$response .= '<Gather action="' . $action
                            . '" method="' . $method
                            . '" timeout="' . $timeout
                            . '" finishOnKey="' . $end_key
                            . '" numDigits="' . $num_digits
                            . '" >';*/
          if(!$text) {
		    $text = $script->evalString($cmd->getParam('url'));
          }
		  $params = array(
		    'timeout' => $timeout,
			'choices' => $choices,
		  );
          $tropo->ask($text, $params);
          // $response .= '</Gather>';
          // in case of timeout or empty input, set Digits to VoipScript::NO_INPUT
          /*$action_timeout = $action . '&amp;Digits=' . VoipScript::NO_INPUT;
          $response .= '<Redirect method="GET">'
                         . $action_timeout
                         . '</Redirect>';*/
		  //TODO:				 
		  $tropo->on(array("event" => "continue", "next" => $action));
          //$tropo->on(array("event" => "incomplete", "next" => "http://whatsupserver.media.mit.edu/tamer/voiptest/outbound_call.php?uri=end"));			  
          $rc = TRUE;
          $processing = FALSE;
          break;

        case 'VoipCmdGosub':
          $script_name = $script->evalString($cmd->getParam('script_name'));
          $rc = $script->runGosub($script_name);
          if($script->isError()){
              $msg = $script->getErrorMessage();
              $params = array();
              $call->report_error('voiptropo', $msg, $params);
              $rc = FALSE;
              $processing = FALSE;
              $found = TRUE;
              $text = "Processing error in gosub command.";
			  $langcode = $language;
              $text = t($text, NULL, $langcode);
			  $tropo->say($text);
          }
          break;

        case 'VoipCmdGoto';
          $label_name = $script->evalString($cmd->getParam('label_name', ''));
          $rc = $script->runGoto($label_name);
          if($script->isError()){
            $msg = $script->getErrorMessage();
            $params = array();
            $call->report_error('voiptropo', $msg, $params);
            $rc = FALSE;
            $processing = FALSE;
            $found = TRUE;
            //$response .= "<Say>$msg.</Say>";
			$tropo->say($msg);
          }
          break;

        case 'VoipCmdGotoIf';
          $condition = $script->evalString($cmd->getParam('condition', ''));
          if($condition) { 
            $label_name = $script->evalString($cmd->getParam('label_name', ''));
            $rc = $script->runGoto($label_name, $condition);
            if($script->isError()){
              $msg = $script->getErrorMessage();
              $params = array();
              $call->report_error('voiptropo', $msg, $params);
              $rc = FALSE;
              $processing = FALSE;
              $found = TRUE;
              //$response .= "<Say>$msg.</Say>";
			  $tropo->say($msg);
            }
          }
          break;

        case 'VoipCmdHangup':
          $call->setCallStatus(VoipCall::IN_PROGRESS);
          $call->setHangupReason(VoipCall::HANGUP_SCRIPT_COMMAND);
          // send the hangup request and stop processing script commands
          // Note: this will trigger a response from tropo with $tropo_action = process_hangup and CallStatus = completed
          //$response .= '<Hangup/>';
		  $tropo->hangup();
          $processing = FALSE;
          break;

        case 'VoipCmdJoinConference';
          $call->setCallStatus(VoipCall::IN_PROGRESS);
          $menu_path = 'voip/tropo/callhandler';
          $call_nid = $call->getNodeId();
          $response_url = $menu_path . '/process_conference/' . $call_nid;
          $action = url($response_url, array('absolute' => TRUE));
          $method = 'POST';
          $room = $script->evalString($cmd->getParam('room'));
          $muted =  $script->evalString($cmd->getParam('muted', false));
          $muted =  $muted? 'true':'false';
          $beep =  $script->evalString($cmd->getParam('beep', false));
          $beep =  $beep? 'true':'false';
          $start_on_enter =  $script->evalString($cmd->getParam('start_on_enter', false));
          $start_on_enter =  $start_on_enter? 'true':'false';
          $end_on_exit =  $script->evalString($cmd->getParam('end_on_exit', false));
          $end_on_exit =  $end_on_exit? 'true':'false';
          $wait_url =  $script->evalString($cmd->getParam('wait_url', ''));
          $hangup_on_star =  $script->evalString($cmd->getParam('hangup_on_star', false));
          $hangup_on_star =  $hangup_on_star? 'true':'false';
          $time_limit = $script->evalString($cmd->getParam('time_limit', '3600'));
          $response .= '<Dial action="' . $action
                            . '" method="' . $method
                            . '" hangupOnStar="' . $hangup_on_star
                            . '" timeLimit="' . $time_limit
                            . '" >';
          $response .= '<Conference muted="' . $muted
                            . '" beep="' . $beep
                            . '" startConferenceOnEnter="' . $start_on_enter
                            . '" endConferenceOnExit="' . $end_on_exit
                            . '" waitUrl="' . $wait_url
                            . '" >';
          $response .= $room;
          $response .= '</Conference>';
          $response .= '</Dial>';
          $rc = TRUE;
          $processing = FALSE;
          break;

        case 'VoipCmdLabel':
          // don't do anything. Labels are just placeholders that mark a position within the script.
          break;

        case 'VoipCmdLoad':
          $script_name = $script->evalString($cmd->getParam('script_name', ''));
          $rc = $script->runLoad($script_name);
          if($script->isError()){
            $msg = $script->getErrorMessage();
            $params = array();
            $call->report_error('voiptropo', $msg, $params);
            $rc = FALSE;
            $processing = FALSE;
            $found = TRUE;
            //$response .= "<Say>$msg.</Say>";
			$tropo->say($msg);
          }
          break;

        case 'VoipCmdPlay':
          $call->setCallStatus(VoipCall::IN_PROGRESS);
          $url = $script->evalString($cmd->getParam('url'));
          $loop = $script->evalString($cmd->getParam('loop', 1));
          //$response .= '<Play loop="' . $loop . '" >' . $url . '</Play>';
		  foreach($loop as $i){
		    $tropo->say($url);
		  }
          break;

        case 'VoipCmdRecord':
          $call->setCallStatus(VoipCall::IN_PROGRESS);
          $menu_path = 'voip/tropo/callhandler';
          $call_nid = $call->getNodeId();
          $format = $script->evalString($cmd->getParam('format'));
          if($format == 'mp3') {
            $response_url = $menu_path . '/process_record_mp3/' . $call_nid;
          }
          else {
            // return wav file
            $response_url = $menu_path . '/process_record/' . $call_nid;
          }
          $action = url($response_url, array('absolute' => TRUE));
          $method = 'POST';
          $timeout = $script->evalString($cmd->getParam('timeout', 5));
          $end_key = $script->evalString($cmd->getParam('end_key', '#'));
          $max_length = $script->evalString($cmd->getParam('max_length', 3600));
          // TODO: implement the "transcribe" feature?"
          $play_beep = $script->evalString($cmd->getParam('play_beep', 'true'));
          $response .= '<Record action="' . $action
                            . '" method="' . $method
                            . '" timeout="' . $timeout
                            . '" finishOnKey="' . $end_key
                            . '" maxLength="' . $max_length
                            . '" playBeep="' . $play_beep
                            . '" />';
          // in case of empty recording...
          $response_url = $menu_path . '/process_empty_recording/' . $call_nid;
          $action = url($response_url, array('absolute' => TRUE));
          $response .= '<Redirect method="GET">'
                         . $action
                         . '</Redirect>';
          $rc = TRUE;
          $processing = FALSE;
          break;

        case 'VoipCmdReject':
          $call->setHangupReason(VoipCall::HANGUP_REJECTED);
          //TODO: Is there reason param in Tropo?
		  //$reason_tmp = $script->evalString($cmd->getParam('reason'));
          //$reason =($reason_tmp == VoipScript::REJECT_BUSY)?'busy':'rejected';
          //$response .= "<Reject reason='$reason' />";
		  $tropo->reject();
          $rc = TRUE;
          $processing = FALSE;
          break;

        case 'VoipCmdReturn':
          $rc = $script->runReturn();
          if($script->isError()){
            $msg = $script->getErrorMessage();
            $params = array();
            $call->report_error('voiptropo', $msg, $params);
            $rc = FALSE;
            $processing = FALSE;
            $found = TRUE;
            // report the error to the phone user
            $text = 'Processing error. Please contact your system administrator.';
            // translate the text
            $langcode = $language;
            $text = t($text, NULL, $langcode);
            //$response .= "<Say voice='$voice' language='$language'>$text</Say>";
			$tropo->say($text);
          }
          break;

        case 'VoipCmdSay':
          $call->setCallStatus(VoipCall::IN_PROGRESS);
          $text = $script->evalString($cmd->getParam('text'));
          $loop = $script->evalString($cmd->getParam('loop'));
          // determine the language to be used
          $cmd_language = $script->evalString($cmd->getParam('language'));
          $cmd_language = $cmd_language ? $cmd_language : $script->getLanguage();
          // determine the voice to be used
          $cmd_voice = $script->evalString($cmd->getParam('voice'));
          $cmd_voice = $cmd_voice ? $cmd_voice : $script->getVoice();
          // translate the text
          $langcode = $cmd_language;
          $text = t($text, NULL, $langcode);
          /*$response .= "<Say voice='$voice' language='$language' "
                       . "loop='$loop'>$text</Say>";*/
		  $tropo->setVoice($cmd_voice);		   
		  $tropo->say($text);	
          break;

        case 'VoipCmdSendSms';
          $call->setCallStatus(VoipCall::IN_PROGRESS);
          $menu_path = 'voip/tropo/callhandler';
          $call_nid = $call->getNodeId();
          $response_url = $menu_path . '/process_send_sms/' . $call_nid;
          $action = url($response_url, array('absolute' => TRUE));
          $callback_url = $menu_path . '/process_sms_callback/' . $call_nid;
          $callback = url($callback_url, array('absolute' => TRUE));
          $method = 'POST';
          $number = $script->evalString($cmd->getParam('number'));
          $text = $script->evalString($cmd->getParam('text'));
          // Note: If no caller number provided, tropo will automatically try
          //       to determine an appropriate value depending on whether the
          //       call is inbound or outbound. However, since that number might
          //       not be sms capable, it might incur in a processing error.
          // Use the system's number as the default caller id
          $default_cid_number = variable_get('voipcall_cid_number', NULL);
          $caller_id = $script->evalString($cmd->getParam('caller_id',
                                                         $default_cid_number)); 
          $response .= '<Sms action="' . $action
                            . '" statusCallback="' . $callback
                            . '" method="' . $method;
          if($number){
            $response .= '" to="' . $number;
          }
          if($caller_id){
            $response .= '" from="' . $caller_id;
          }
          $response .= '" >';
          $response .= $text;
          $response .= '</Sms>';
          $rc = TRUE;
          $processing = FALSE;
          break;

        case 'VoipCmdSet';
          $var_name = $script->evalString($cmd->getParam('var_name'));
          $var_value = $script->evalString($cmd->getParam('var_value'));
          $script->setVar($var_name, $var_value);
          break;

        case 'VoipCmdSetLanguage';
          $language = $script->evalString($cmd->getParam('language'));
          $script->setLanguage($language);
          break;

        case 'VoipCmdSetVoice';
          $voice = $script->evalString($cmd->getParam('voice'));
          $script->setVoice($voice);
          break;

        case 'VoipCmdUnset';
          $var_name = $script->evalString($cmd->getParam('var_name'));
          $script->unsetVar($var_name);
          break;

      }

      // go to next command
    }
  }

  // save script for next iteration
  $call->setScript($script);
  $call->save();

  // send response back to tropo
  $tropo->renderJSON();

  return $rc;
}

/**
 * Update 'call status' and 'hangup reason' based on tropo's response
 */
function voiptropo_update_call_status($call, $tropo_call_status) {
  switch($tropo_call_status) {
    default:
      $msg = 'Invalid CallStatus: @status for call nid: @nid';
      $call_nid = $call->getNodeId();
      $params = array('@status' => $call_status, '@nid' => $call_nid);
      $call->report_error('voiptropo', $msg, $params);
      return NULL;
      break;
    case 'failed': // call failed, most likely due to invalid number
      $call->setCallStatus(VoipCall::INVALID);
      $call->setHangupReason(VoipCall::HANGUP_INVALID_NUMBER);
      break;
    case 'queued':
      $call->setCallStatus(VoipCall::QUEUED);
      break;
    case 'ringing':
      $call->setCallStatus(VoipCall::RINGING);
      break;
    case 'in-progress':
      $call->setCallStatus(VoipCall::IN_PROGRESS);
      break;
    case 'completed':
      $call->setCallStatus(VoipCall::COMPLETED);
      $hangup_reason = $call->getHangupReason();
      if(!$hangup_reason){
        $call->setHangupReason(VoipCall::HANGUP_BY_USER);
      }
      break;
    case 'busy':
      $call->setCallStatus(VoipCall::BUSY);
      $call->setHangupReason(VoipCall::HANGUP_BUSY);
      break;
    case 'no-answer':
      $call->setCallStatus(VoipCall::NO_ANSWER);
      $call->setHangupReason(VoipCall::HANGUP_NO_ANSWER);
      break;
    case 'canceled':
      $call->setCallStatus(VoipCall::CANCELED);
      $call->setHangupReason(VoipCall::HANGUP_API_REQUEST);
      break;
  }
  return $call;
}


/**
 * Process the response to a "dial" script command
 */ 
function _voiptropo_process_dial_response($call, $request){
  $script = $call->getScript();

  // update the 'dial_sid' script variable
  $script->setVar('dial_sid', $request['DialCallSid']);

  // update the 'dial_status' script variable
  $dial_status = $request['DialCallStatus'];
  switch($dial_status) {
    default:
      $call_nid = $call->getNodeId();
      $msg = 'Invalid DialCallStatus: @status for call nid: @nid';
      $params = array('@status' => $dial_status, '@nid' => $call_nid);
      $call->report_error('voiptropo', $msg, $params);
      return NULL;
      break;
    case 'completed':
      $script->setVar('dial_status', VoipScript::DIAL_COMPLETED);
      break;
    case 'busy':
      $script->setVar('dial_status', VoipScript::DIAL_BUSY);
      break;
    case 'no-answer':
      $script->setVar('dial_status', VoipScript::DIAL_NO_ANSWER);
      break;
    case 'failed':
      $script->setVar('dial_status', VoipScript::DIAL_FAILED);
      break;
    case 'canceled':
      $script->setVar('dial_status', VoipScript::DIAL_CANCELED);
      break;
  }

  // update the 'dial_duration' script variable
  $duration = $request['DialCallDuration'];
  if(isset($duration)){
    $script->setVar('dial_duration', $duration);
  }

  $call->setScript($script);
  return $call;
}

/**
 * Process the response to a 'sms send' script command
 */
function _voiptropo_process_sms_response($call, $request){
  $call_nid = $call->getNodeId();
  $sms_status_tmp = $request['SmsStatus'];
  switch($sms_status_tmp) {
    default:
      $msg = 'Invalid SmsStatus: @status for call nid: @nid';
      $params = array('@status' => $sms_status_tmp, '@nid' => $call_nid);
      $call->report_error('voiptropo', $msg, $params);
      return NULL;
      break;
    case 'failed':
      $msg = 'SMS sendind failed for call nid: @nid. Make sure your tropo number is sms-enabled.';
      $params = array('@nid' => $call_nid);
      watchdog('voiptropo', $msg, $params, WATCHDOG_WARNING);
      $sms_status = VoipScript::SMS_FAILED;
      break;
    case 'invalid':
      $msg = 'Invalid SMS parameter for call nid: @nid';
      $params = array('@nid' => $call_nid);
      watchdog('voiptropo', $msg, $params, WATCHDOG_WARNING);
      $sms_status = VoipScript::SMS_INVALID;
      break;
    case 'sending':
      $sms_status = VoipScript::SMS_SENDING;
      break;
    case 'sent':
      $sms_status = VoipScript::SMS_SENT;
      break;
  }
  $call->setSmsStatus($sms_status);
  return $call;
}
